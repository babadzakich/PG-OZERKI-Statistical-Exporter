package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.util.*;
import java.util.concurrent.ThreadLocalRandom;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;

@Slf4j
public class MarkovGenerator implements UniqueKeyGenerator {
    private static final int MARKOV_MAX_ATTEMPTS_PER_ITEM = 200;

    private final List<ColumnMetadata> columnsMetadata;
    private final int recordCount;
    private final Map<String, List<ReferencingTreeNode>> referencingTrees;
    private final Map<String, List<Object>> existingData;
    private final ThreadLocalRandom random = ThreadLocalRandom.current();

    public MarkovGenerator(List<ColumnMetadata> columnsMetadata, int recordCount,
                           Map<String, List<ReferencingTreeNode>> referencingTrees,
                           Map<String, List<Object>> allGeneratedData) {
        this.columnsMetadata = columnsMetadata;
        this.referencingTrees = referencingTrees;
        this.recordCount = recordCount;
        this.existingData = allGeneratedData;
    }

    @Override
    public void generate(Map<String, List<Object>> columnData) {
        log.info("Запуск Markov генератора для {} уникальных записей и колонок {}", recordCount, columnsMetadata.stream().map(ColumnMetadata::getName).toList());
        List<List<Object>> uniqueValues = generateUnique(recordCount, 200);

        for (int colIdx = 0; colIdx < columnsMetadata.size(); colIdx++) {
            List<Object> columnValues = new ArrayList<>();
            for (List<Object> uniqueValue : uniqueValues) {
                columnValues.add(uniqueValue.get(colIdx));
            }
            columnData.put(columnsMetadata.get(colIdx).getName(), columnValues);
        }
    }

    @Override
    public List<Object> generate() {
        throw new UnsupportedOperationException("Markov generator can only be used for Multiple column unique, " +
                "use generate(Map<String, List<Object>> columnData) instead.");
    }

    public List<List<Object>> generateUnique(int count, int maxAttemptsPerItem) {

        // Шаг 1: для каждой колонки строим точный набор уникальных значений размером ndistinct
        Map<String, List<Object>> colValueSets = new LinkedHashMap<>();
        for (int i = 0; i < columnsMetadata.size(); i++) {
            int finalI = i;
            colValueSets.computeIfAbsent(columnsMetadata.get(i).getName(), k -> buildUniqueValueSet(finalI, colValueSets));
        }

        // Шаг 2: строим пул для каждой колонки — каждое значение встречается ровно count/ndistinct раз
        List<List<Object>> colPools = new ArrayList<>();
        for (var valueSet : colValueSets.keySet()) {
            colPools.add(buildExactPool(colValueSets.get(valueSet), count));
        }

        // Шаг 3: зиппуем перемешанные пулы в строки
        Set<List<Object>> uniques = new HashSet<>();

        for (int rowIdx = 0; rowIdx < count; rowIdx++) {
            List<Object> seq = new ArrayList<>(columnsMetadata.size());
            for (List<Object> pool : colPools) {
                seq.add(pool.get(rowIdx));
            }
            uniques.add(seq);
        }

        log.debug("После zip: {}/{} уникальных записей", uniques.size(), count);

        // Шаг 4: если коллизий много (пространство комбинаций меньше count) —
        // добираем случайным семплингом из тех же value sets
        if (uniques.size() < count) {
            log.debug("Добираем {} записей случайным семплингом", count - uniques.size());
            int attempts = 0;
            int maxAttempts = (count - uniques.size()) * maxAttemptsPerItem;
            while (uniques.size() < count && attempts < maxAttempts) {
                attempts++;
                List<Object> seq = new ArrayList<>(columnsMetadata.size());
                for (List<Object> valueSet : colValueSets.values()) {
                    seq.add(valueSet.get(random.nextInt(valueSet.size())));
                }
                uniques.add(seq);
            }
        }

        // Шаг 5: если пространство исчерпано — расширяем value sets синтетическими значениями
        if (uniques.size() < count) {
            log.debug("Пространство комбинаций исчерпано. Расширяем синтетическими значениями...");
            for (int i = 0; i < columnsMetadata.size(); i++) {
                if (columnsMetadata.get(i).isForeignKey()) {
                    continue;
                }
                expandValueSet(colValueSets.get(columnsMetadata.get(i).getName()), count, i);
            }
            int attempts = 0;
            int maxAttempts = count * maxAttemptsPerItem;
            while (uniques.size() < count && attempts < maxAttempts) {
                attempts++;
                List<Object> seq = new ArrayList<>(columnsMetadata.size());
                for (List<Object> valueSet : colValueSets.values()) {
                    seq.add(valueSet.get(random.nextInt(valueSet.size())));
                }
                uniques.add(seq);
            }
            if (uniques.size() < count) {
                throw new RuntimeException(
                        "Не удалось получить требуемое количество уникальных элементов ("
                                + count + "). Получено только: " + uniques.size()
                );
            }
        }

        log.debug("Итого уникальных записей: {}", uniques.size());
        return uniques.stream().toList();
    }

    /**
     * Строит точный набор уникальных значений для колонки colIdx:
     * - все MCV-значения (гарантируем их присутствие в данных)
     * - остаток до ndistinct генерируется через ValueGenerator или берется из FK
     */
    private List<Object> buildUniqueValueSet(int colIdx, Map<String, List<Object>> colValueSets) {
        ColumnMetadata colMeta = columnsMetadata.get(colIdx);
        Map<Object, Double> mcv = colMeta.getMcv();
        List<Object> histogram = colMeta.getHistogramm();
        double ndistinct = colMeta.getNdistinct();
        int totalUnique = (int) (ndistinct < 0 ? -ndistinct * recordCount : ndistinct);

        totalUnique = Math.max(totalUnique, mcv.size());

        Set<Object> seen = new HashSet<>(mcv.keySet());
        List<Object> valueSet = new ArrayList<>(seen);

        List<ReferencingTreeNode> trees = referencingTrees != null
                ? referencingTrees.get(colMeta.getName())
                : null;
        if (trees != null) {
            for (ReferencingTreeNode node : trees) {
                collectReferencedValues(seen, valueSet, node);
            }
        }

        totalUnique = Math.max(totalUnique, valueSet.size());

        log.debug("Колонка {}: ndistinct={}, totalUnique={}, mcv.size()={}, referenced={}",
                colMeta.getName(), ndistinct, totalUnique, mcv.size(), valueSet.size() - mcv.size());

        int toAdd = totalUnique - valueSet.size();
        if (toAdd > 0) {
            if (colMeta.isForeignKey()) {
                boolean compositeHandled = false;

                if (colMeta.getCompositeForeignPeers() != null && !colMeta.getCompositeForeignPeers().isEmpty()) {
                    List<String> peerColumns = colMeta.getCompositeForeignPeers().getFirst().stream().toList();
                    if (!peerColumns.isEmpty()) {
                        int selfPos = peerColumns.indexOf(colMeta.getName());
                        if (selfPos >= 0 && selfPos < colMeta.getForeignKeyMetadata().size()) {
                            List<List<Object>> parentColumnsData = new ArrayList<>();
                            for (int i = 0; i < peerColumns.size() && i < colMeta.getForeignKeyMetadata().size(); i++) {
                                var fkMeta = colMeta.getForeignKeyMetadata().get(i);
                                String refKey = fkMeta.getReferencedSchema() + '.' + fkMeta.getReferencedTable() + '.' + fkMeta.getReferencedColumn();
                                List<Object> parentData = existingData.get(refKey);
                                if (parentData == null || parentData.isEmpty()) {
                                    log.warn("Для composite FK колонки {} не найдены данные родительской колонки (ключ {}).", colMeta.getName(), refKey);
                                    parentColumnsData.clear();
                                    break;
                                }
                                parentColumnsData.add(parentData);
                            }

                            if (!parentColumnsData.isEmpty()) {
                                int minSize = parentColumnsData.stream().mapToInt(List::size).min().orElse(0);
                                if (minSize > 0) {
                                    List<Integer> indices = new ArrayList<>(minSize);
                                    for (int i = 0; i < minSize; i++) {
                                        indices.add(i);
                                    }
                                    Collections.shuffle(indices, random);

                                    List<Object> selfParentColumn = parentColumnsData.get(selfPos);
                                    for (int idx : indices) {
                                        if (valueSet.size() >= totalUnique) break;
                                        Object val = selfParentColumn.get(idx);
                                        if (seen.add(val)) {
                                            valueSet.add(val);
                                        }
                                    }
                                    compositeHandled = true;
                                }
                            }
                        }
                    }
                }

                if (!compositeHandled) {
                    List<Set<Object>> validCandidatesPerFk = new ArrayList<>();
                    for (var fk : colMeta.getForeignKeyMetadata()) {
                        String refKey = fk.getReferencedSchema() + '.' + fk.getReferencedTable() + '.' + fk.getReferencedColumn();
                        List<Object> parentData = existingData.get(refKey);

                        if (parentData != null && !parentData.isEmpty()) {
                            validCandidatesPerFk.add(new HashSet<>(parentData));
                        } else {
                            log.warn("Для FK колонки {} не найдены данные родительской таблицы (ключ {}).", colMeta.getName(), refKey);
                        }
                    }

                    if (!validCandidatesPerFk.isEmpty()) {
                        Set<Object> intersection = new HashSet<>(validCandidatesPerFk.getFirst());
                        for (int k = 1; k < validCandidatesPerFk.size(); k++) {
                            intersection.retainAll(validCandidatesPerFk.get(k));
                        }

                        List<Object> candidates = new ArrayList<>(intersection);
                        Collections.shuffle(candidates, random);
//                        valueSet.add(candidates);
                        for (Object val : candidates) {
                            if (valueSet.size() >= totalUnique) break;
                            if (seen.add(val)) {
                                valueSet.add(val);
                            }
                        }
                    }
                }
            } else {
                ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(colMeta);
                List<Object> extra = new ArrayList<>(toAdd);
                if (histogram != null && !histogram.isEmpty()) {
                    generator.generateValues(extra, toAdd, histogram.getFirst(), histogram.getLast());
                } else {
                    generator.generateValues(extra, toAdd);
                }
                for (Object v : extra) {
                    if (seen.add(v)) {
                        valueSet.add(v);
                    }
                }

                int safetyLimit = toAdd * 10;
                int att = 0;
                while (valueSet.size() < totalUnique && att < safetyLimit) {
                    att++;
                    Object v = generator.generateValue();
                    if (seen.add(v)) {
                        valueSet.add(v);
                    }
                }
            }
        }

        log.debug("Колонка {}: итого уникальных значений={}", colMeta.getName(), valueSet.size());
        return valueSet;
    }

    /**
     * Рекурсивно собирает значения из дерева обратных зависимостей.
     * Эти значения должны обязательно присутствовать в сгенерированных данных,
     * чтобы FK-колонки дочерних таблиц могли на них ссылаться.
     */
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

    /**
     * Расширяет value set синтетическими значениями когда пространства комбинаций не хватает.
     */
    private void expandValueSet(List<Object> valueSet, int count, int colIdx) {
        List<Object> histogram = columnsMetadata.get(colIdx).getHistogramm();
        ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(columnsMetadata.get(colIdx));

        int targetSize = (int) Math.ceil(Math.sqrt((double) count)) * 2;

        if (!histogram.isEmpty() && histogram.getFirst() instanceof Number && histogram.getLast() instanceof Number) {
            long rangeSize = ((Number) histogram.getLast()).longValue() - ((Number) histogram.getFirst()).longValue();
            targetSize = (int) Math.min(targetSize, rangeSize);
        }

        int toAdd = targetSize - valueSet.size();
        if (toAdd <= 0) return;

        Set<Object> existing = new HashSet<>(valueSet);
        List<Object> extra = new ArrayList<>(toAdd);
        if (!histogram.isEmpty()) {
            generator.generateValues(extra, toAdd, histogram.getFirst(), histogram.getLast());
        } else {
            generator.generateValues(extra, toAdd);
        }
        for (Object v : extra) {
            if (existing.add(v)) {
                valueSet.add(v);
            }
        }
        log.debug("Расширена колонка {}: теперь {} уникальных значений", columnsMetadata.get(colIdx).getName(), valueSet.size());
    }

    /**
     * Строит пул размером count из valueSet так, что каждое значение встречается
     * ровно count/size (или +1) раз — гарантируя точный ndistinct.
     * Пул перемешивается перед возвратом.
     */
    private List<Object> buildExactPool(List<Object> valueSet, int count) {
        List<Object> pool = new ArrayList<>(count);
        int size = valueSet.size();
        int base = count / size;
        int extra = count % size;

        for (int i = 0; i < size; i++) {
            int times = base + (i < extra ? 1 : 0);
            for (int k = 0; k < times; k++) {
                pool.add(valueSet.get(i));
            }
        }

        Collections.shuffle(pool, random);
        return pool;
    }
}
