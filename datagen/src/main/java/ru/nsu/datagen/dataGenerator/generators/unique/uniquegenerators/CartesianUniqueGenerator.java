package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.util.*;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;
import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

/**
 * Генератор уникальных комбинаций для многоколончатых UNIQUE/PK ключей через декартово произведение.
 *
 * <p>Для каждой колонки заранее строится список из {@code ndistinct} различных значений:
 * <ol>
 *   <li>обязательные значения, на которые ссылаются дочерние таблицы ({@code referencingTrees});</li>
 *   <li>MCV-значения, отсортированные по убыванию частоты;</li>
 *   <li>добор до {@code ndistinct}: FK — из данных родителя, не-FK — через {@link ValueGeneratorFactory}.</li>
 * </ol>
 *
 * <p>Строка с номером {@code n} — это n-й элемент декартова произведения (смешанная система счисления):
 * {@code idx_0 = n % len_0}, {@code idx_1 = (n / len_0) % len_1}, ... Первая колонка берёт значение
 * просто по {@code n % len_0}, следующие — по остатку от деления «частного» на длину своего массива.
 * Так строки гарантированно уникальны для всех {@code n < len_0 * len_1 * ...} без какого-либо дедупа.
 *
 * <p>Составные FK внутри ключа образуют одно «измерение» из кортежей родителя, чтобы не ломать FK.
 * Самым старшим разрядом выбирается не-FK измерение: дописывание значений в его конец не меняет
 * уже выданные строки, поэтому при исчерпании пространства оно расширяется лениво.
 *
 * <p>Номер следующей строки хранится внутри генератора и только растёт: откат
 * {@code StateData.generatedCount} после неудачной вставки не приводит к повторной выдаче тех же строк.
 */
@Slf4j
public class CartesianUniqueGenerator implements UniqueKeyGenerator {
    private static final int GENERATION_ATTEMPTS_FACTOR = 10;

    private final List<ColumnMetadata> columnsMetadata;
    private final int recordCount;
    private final Map<String, List<ReferencingTreeNode>> referencingTrees;
    private final Map<String, List<Object>> existingData;

    private record Dimension(List<Integer> colIndices, List<List<Object>> tuples) {}

    private final List<Dimension> dimensions;
    private long nextRow;

    /**
     * @param columnsMetadata  метаданные колонок составного ключа
     * @param recordCount      целевое число записей в таблице
     * @param referencingTrees дерево обратных FK-ссылок по имени колонки (для обязательных значений)
     * @param allGeneratedData данные уже сгенерированных родительских таблиц (ключ: {@code schema.table.column})
     */
    public CartesianUniqueGenerator(List<ColumnMetadata> columnsMetadata, int recordCount,
                                    Map<String, List<ReferencingTreeNode>> referencingTrees,
                                    Map<String, List<Object>> allGeneratedData) {
        this.columnsMetadata = columnsMetadata;
        this.recordCount = recordCount;
        this.referencingTrees = referencingTrees;
        this.existingData = allGeneratedData;
        this.dimensions = buildDimensions();
        ensureCapacity(recordCount);
        log.info("Cartesian генератор для колонок {}: размеры измерений {}, пространство {}",
                columnsMetadata.stream().map(ColumnMetadata::getName).toList(),
                dimensions.stream().map(d -> d.tuples().size()).toList(), capacity());
    }

    @Override
    public List<Object> generate() {
        throw new UnsupportedOperationException("Cartesian generator is for multi-column unique keys.");
    }

    @Override
    public void generate(Map<String, List<Object>> columnData) {
        List<List<Object>> batch = nextRows(recordCount);
        for (int i = 0; i < columnsMetadata.size(); i++) {
            columnData.put(columnsMetadata.get(i).getName(), batch.get(i));
        }
    }

    @Override
    public void generate(Map<String, List<Object>> columnData, int offset, int batchSize) {
        List<List<Object>> batch = rowsAt(offset, batchSize);
        for (int i = 0; i < columnsMetadata.size(); i++) {
            columnData.put(columnsMetadata.get(i).getName(), batch.get(i));
        }
    }

    @Override
    public List<List<Object>> generateValues(int batchSize, StateData stateData) {
        List<List<Object>> batch = nextRows(batchSize);
        stateData.advance(batch.getFirst().size());
        return batch;
    }

    @Override
    public List<List<Object>> regenerateRows(int count) {
        if (count <= 0) return List.of();
        return nextRows(count);
    }

    private List<List<Object>> nextRows(int count) {
        ensureCapacity(nextRow + count);
        List<List<Object>> batch = rowsAt(nextRow, count);
        nextRow += batch.getFirst().size();
        return batch;
    }

    private List<List<Object>> rowsAt(long start, int count) {
        long end = Math.min(start + count, capacity());
        int size = (int) Math.max(0, end - start);
        if (size < count) {
            log.warn("Пространство комбинаций исчерпано для колонок {}: запрошено {}, выдано {}",
                    columnsMetadata.stream().map(ColumnMetadata::getName).toList(), count, size);
        }

        List<List<Object>> cols = new ArrayList<>(columnsMetadata.size());
        for (int j = 0; j < columnsMetadata.size(); j++) {
            cols.add(new ArrayList<>(size));
        }
        for (long n = start; n < end; n++) {
            long rest = n;
            for (Dimension dim : dimensions) {
                int len = dim.tuples().size();
                List<Object> tuple = dim.tuples().get((int) (rest % len));
                rest /= len;
                for (int k = 0; k < dim.colIndices().size(); k++) {
                    cols.get(dim.colIndices().get(k)).add(tuple.get(k));
                }
            }
        }
        return cols;
    }

    private long capacity() {
        long product = 1;
        try {
            for (Dimension dim : dimensions) {
                product = Math.multiplyExact(product, (long) dim.tuples().size());
            }
        } catch (ArithmeticException overflow) {
            return Long.MAX_VALUE;
        }
        return product;
    }

    /**
     * Дописывает новые значения в самое старшее измерение, пока пространство меньше {@code required}.
     * Работает только если старшее измерение не-FK; уже выданные строки при этом не меняются.
     */
    private void ensureCapacity(long required) {
        if (capacity() >= required || dimensions.isEmpty()) return;
        Dimension top = dimensions.getLast();
        if (top.colIndices().size() != 1 || columnsMetadata.get(top.colIndices().getFirst()).isForeignKey()) {
            return;
        }
        long others = 1;
        for (int i = 0; i < dimensions.size() - 1; i++) others *= dimensions.get(i).tuples().size();
        if (others == 0) return;

        long needed = (required + others - 1) / others - top.tuples().size();
        if (needed <= 0) return;
        ColumnMetadata colMeta = columnsMetadata.get(top.colIndices().getFirst());
        int before = top.tuples().size();
        Set<Object> seen = new HashSet<>(before);
        for (List<Object> tuple : top.tuples()) seen.add(tuple.getFirst());
        List<Object> fresh = generateFreshValues(colMeta, seen, (int) Math.min(Integer.MAX_VALUE, needed));
        for (Object v : fresh) top.tuples().add(List.of(v));
        log.debug("Колонка {} расширена с {} до {} значений", colMeta.getName(), before, top.tuples().size());
    }

    private List<Dimension> buildDimensions() {
        List<Dimension> result = new ArrayList<>();
        Set<Integer> assigned = new HashSet<>();

        for (int i = 0; i < columnsMetadata.size(); i++) {
            if (assigned.contains(i)) continue;
            List<Integer> group = compositeFkGroup(i);
            if (group.size() > 1) {
                // Колонки составного FK нельзя брать независимо: получится пара, которой нет у родителя.
                List<List<Object>> tuples = buildFkGroupTuples(group);
                if (tuples.isEmpty()) {
                    throw new IllegalStateException("Нет данных родителя для составного FK колонок "
                            + group.stream().map(k -> columnsMetadata.get(k).getName()).toList());
                }
                assigned.addAll(group);
                result.add(new Dimension(group, tuples));
                continue;
            }
            assigned.add(i);
            result.add(buildColumnDimension(i));
        }

        // Самым старшим разрядом делаем самое большое не-FK измерение — его можно расширять.
        Dimension top = null;
        for (Dimension dim : result) {
            if (dim.colIndices().size() == 1 && !columnsMetadata.get(dim.colIndices().getFirst()).isForeignKey()
                    && (top == null || dim.tuples().size() > top.tuples().size())) {
                top = dim;
            }
        }
        if (top != null) {
            result.remove(top);
            result.add(top);
        }
        return result;
    }

    private Dimension buildColumnDimension(int colIdx) {
        ColumnMetadata colMeta = columnsMetadata.get(colIdx);
        // Для FK-колонки допустимы только значения, реально сгенерированные в родительской таблице:
        // MCV и обязательные значения берутся из статистики/детей и у родителя их может не быть.
        Set<Object> parentValues = null;
        if (colMeta.isForeignKey()) {
            parentValues = new LinkedHashSet<>(fkCandidates(colMeta));
            if (parentValues.isEmpty()) {
                throw new IllegalStateException("Нет данных родителя для FK колонки " + colMeta.getName());
            }
        }

        Set<Object> candidates = new LinkedHashSet<>();
        List<ReferencingTreeNode> trees = referencingTrees != null ? referencingTrees.get(colMeta.getName()) : null;
        if (trees != null) {
            List<Object> mandatory = new ArrayList<>();
            for (ReferencingTreeNode node : trees) {
                collectReferencedValues(new HashSet<>(), mandatory, node);
            }
            candidates.addAll(mandatory);
        }
        int mandatoryCount = candidates.size();
        colMeta.getMcv().entrySet().stream()
                .sorted(Map.Entry.<Object, Double>comparingByValue().reversed())
                .forEach(e -> candidates.add(e.getKey()));

        Set<Object> seen = new HashSet<>();
        List<Object> values = new ArrayList<>();
        int dropped = 0;
        for (Object v : candidates) {
            if (parentValues != null && !parentValues.contains(v)) {
                dropped++;
                continue;
            }
            if (seen.add(v)) values.add(v);
        }
        if (dropped > 0) {
            log.warn("FK колонка {}: {} MCV/обязательных значений нет у родителя, они пропущены",
                    colMeta.getName(), dropped);
        }

        int target = Math.max(distinctTarget(colMeta), values.size());
        if (values.size() < target) {
            if (parentValues != null) {
                for (Object v : parentValues) {
                    if (values.size() >= target) break;
                    if (seen.add(v)) values.add(v);
                }
            } else {
                values.addAll(generateFreshValues(colMeta, seen, target - values.size()));
            }
        }
        if (values.isEmpty()) {
            throw new IllegalStateException("Не удалось построить значения для колонки " + colMeta.getName());
        }

        log.debug("Колонка {}: {} значений (mandatory={}, mcv={}, fk={}, target={})",
                colMeta.getName(), values.size(), mandatoryCount, colMeta.getMcv().size(),
                colMeta.isForeignKey(), target);

        List<List<Object>> tuples = new ArrayList<>(values.size());
        for (Object v : values) tuples.add(Collections.singletonList(v));
        return new Dimension(List.of(colIdx), tuples);
    }

    private int distinctTarget(ColumnMetadata colMeta) {
        double nd = colMeta.getNdistinct();
        long target = nd < 0 ? Math.round(-nd * recordCount) : Math.round(nd);
        return (int) Math.max(1, Math.min(target, recordCount));
    }

    /**
     * Генерирует до {@code count} новых значений колонки, которых ещё нет в {@code seen}.
     * Найденные значения добавляются в {@code seen}.
     */
    private List<Object> generateFreshValues(ColumnMetadata colMeta, Set<Object> seen, int count) {
        List<Object> histogram = colMeta.getHistogramm();
        ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(colMeta);
        List<Object> candidates = new ArrayList<>(count);
        if (histogram != null && !histogram.isEmpty()) {
            generator.generateValues(candidates, count, histogram.getFirst(), histogram.getLast());
        } else {
            generator.generateValues(candidates, count);
        }

        List<Object> result = new ArrayList<>(count);
        for (Object v : candidates) {
            if (result.size() >= count) break;
            if (seen.add(v)) result.add(v);
        }
        long maxAttempts = (long) count * GENERATION_ATTEMPTS_FACTOR;
        for (long attempts = 0; result.size() < count && attempts < maxAttempts; attempts++) {
            Object v = generator.generateValue();
            if (seen.add(v)) result.add(v);
        }
        if (result.size() < count) {
            log.warn("Колонка {}: удалось сгенерировать только {} из {} новых уникальных значений",
                    colMeta.getName(), result.size(), count);
        }
        return result;
    }

    private List<Object> fkCandidates(ColumnMetadata colMeta) {
        Set<Object> intersection = null;
        for (var fk : colMeta.getForeignKeyMetadata()) {
            String refKey = fk.getReferencedSchema() + '.' + fk.getReferencedTable() + '.' + fk.getReferencedColumn();
            List<Object> parentData = existingData.get(refKey);
            if (parentData == null || parentData.isEmpty()) {
                log.warn("Для FK колонки {} не найдены данные родительской таблицы (ключ {}).", colMeta.getName(), refKey);
                continue;
            }
            if (intersection == null) {
                intersection = new LinkedHashSet<>(parentData);
            } else {
                intersection.retainAll(new HashSet<>(parentData));
            }
        }
        return intersection == null ? List.of() : new ArrayList<>(intersection);
    }

    /** Индексы колонок ключа, входящих в тот же составной FK, что и колонка {@code colIdx}. */
    private List<Integer> compositeFkGroup(int colIdx) {
        ColumnMetadata col = columnsMetadata.get(colIdx);
        if (!col.isForeignKey()) return List.of(colIdx);
        List<List<String>> peers = col.getCompositeForeignPeers();
        if (peers == null || peers.isEmpty() || peers.getFirst().isEmpty()) return List.of(colIdx);

        List<Integer> group = new ArrayList<>();
        for (int j = 0; j < columnsMetadata.size(); j++) {
            String name = columnsMetadata.get(j).getName();
            for (String peer : peers.getFirst()) {
                if (peer.endsWith("." + name)) {
                    group.add(j);
                    break;
                }
            }
        }
        return group;
    }

    private List<List<Object>> buildFkGroupTuples(List<Integer> groupIndices) {
        List<List<Object>> parentColumnData = new ArrayList<>();
        for (int colIdx : groupIndices) {
            ColumnMetadata col = columnsMetadata.get(colIdx);
            List<String> peerGroup = col.getCompositeForeignPeers().getFirst();

            int selfPos = -1;
            for (int i = 0; i < peerGroup.size(); i++) {
                if (peerGroup.get(i).endsWith("." + col.getName())) {
                    selfPos = i;
                    break;
                }
            }
            if (selfPos < 0 || selfPos >= col.getForeignKeyMetadata().size()) {
                log.warn("Не удалось определить FK позицию для колонки {}", col.getName());
                return Collections.emptyList();
            }

            var fkMeta = col.getForeignKeyMetadata().get(selfPos);
            String refKey = fkMeta.getReferencedSchema() + '.' + fkMeta.getReferencedTable() + '.' + fkMeta.getReferencedColumn();
            List<Object> data = existingData.get(refKey);
            if (data == null || data.isEmpty()) {
                log.warn("Нет данных родителя для FK колонки {} (ключ {})", col.getName(), refKey);
                return Collections.emptyList();
            }
            parentColumnData.add(data);
        }

        int rowCount = parentColumnData.getFirst().size();
        Set<List<Object>> seen = new LinkedHashSet<>();
        for (int i = 0; i < rowCount; i++) {
            List<Object> tuple = new ArrayList<>(groupIndices.size());
            for (List<Object> colData : parentColumnData) {
                tuple.add(i < colData.size() ? colData.get(i) : null);
            }
            seen.add(tuple);
        }
        return new ArrayList<>(seen);
    }

    private void collectReferencedValues(Set<Object> seen, List<Object> valueSet, ReferencingTreeNode node) {
        for (Object val : node.getToAdd()) {
            if (seen.add(val)) {
                valueSet.add(val);
            }
        }
        for (ReferencingTreeNode child : node.getChildren()) {
            collectReferencedValues(seen, valueSet, child);
        }
    }
}
