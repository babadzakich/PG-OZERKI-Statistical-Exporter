package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.util.*;
import java.util.concurrent.ThreadLocalRandom;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;
import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

/**
 * Генератор уникальных комбинаций для многоколончатых UNIQUE/PK ключей.
 *
 * <p>Каждая колонка представлена взвешенным распределением ({@code colValues} + {@code colCumWeights}):
 * <ul>
 *   <li>MCV-значения получают вес из статистики PostgreSQL ({@code pg_stats.most_common_freqs}).</li>
 *   <li>Не-MCV значения делят поровну оставшуюся вероятность ({@code 1 - sumMcvFreqs}).</li>
 *   <li>FK-колонки берут значения только из данных родительской таблицы.</li>
 *   <li>Не-FK колонки расширяются через {@link ValueGeneratorFactory} до {@code safeTargetPerColumn} значений.</li>
 * </ul>
 *
 * <p>Алгоритм генерации батча:
 * <ol>
 *   <li>Принудительно вставляются «обязательные» строки ({@code mandatoryValues}) —
 *       значения, на которые ссылаются дочерние таблицы.</li>
 *   <li>Взвешенным сэмплингом (бинарный поиск по кумулятивным весам) генерируются строки
 *       до {@code target}. Дубликаты отбрасываются ({@link LinkedHashSet}).</li>
 *   <li>При застревании (много подряд пустых батчей) веса адаптивно пересчитываются:
 *       часто используемые значения подавляются экспоненциально, инжектируются свежие не-MCV значения.</li>
 * </ol>
 *
 * <p>Составные FK генерируются как кортежи ({@link FkGroupDist}) из данных родителя,
 * чтобы гарантировать соответствие всем FK-столбцам сразу.
 */
@Slf4j
public class MarkovGenerator implements UniqueKeyGenerator {
    private static final int MARKOV_MAX_ATTEMPTS_PER_ITEM = 200;
    private static final double SPACE_SAFETY_FACTOR = 10.0;
    private static final double WEIGHT_DECAY = 0.999;
    // Чем больше подряд пустых батчей, тем агрессивнее давим часто использованные значения.
    // stuckFactor = 1 + emptyBatches * BOOST_PER_EMPTY масштабирует показатель decay по used[i].
    private static final double BOOST_PER_EMPTY = 5.0;
    // Дополнительное подавление MCV в режиме застревания (поверх decay по used).
    private static final double MCV_SUPPRESSION_BASE = 0.5;
    private static final int STUCK_FACTOR_EMPTY_CAP = 20;

    private final List<ColumnMetadata> columnsMetadata;
    private final int recordCount;
    private final Map<String, List<ReferencingTreeNode>> referencingTrees;
    private final Map<String, List<Object>> existingData;
    private final ThreadLocalRandom random = ThreadLocalRandom.current();

    private static final int REBUILD_INTERVAL = 500;

    private List<List<Object>> colValues;
    private List<double[]> colCumWeights;
    private List<double[]> colOrigWeights;
    private List<int[]> colUsedCounts;
    private List<Map<Object, Integer>> colValueIndex;
    private List<Integer> colMcvCounts;
    private int safeTargetPerColumn;
    private int dirtyCount;
    private int lastEmptyBatchSeen = -1;
    private int currentEmptyBatches;
    private List<List<Object>> mandatoryValues;

    private record FkGroupDist(List<Integer> colIndices, List<List<Object>> tuples) {}
    private List<FkGroupDist> fkGroups = Collections.emptyList();

    /**
     * @param columnsMetadata    метаданные колонок составного ключа
     * @param recordCount        целевое число записей в таблице
     * @param referencingTrees   дерево обратных FK-ссылок по имени колонки (для обязательных значений)
     * @param allGeneratedData   данные уже сгенерированных родительских таблиц (ключ: {@code schema.table.column})
     */
    public MarkovGenerator(List<ColumnMetadata> columnsMetadata, int recordCount,
                           Map<String, List<ReferencingTreeNode>> referencingTrees,
                           Map<String, List<Object>> allGeneratedData) {
        this.columnsMetadata = columnsMetadata;
        this.referencingTrees = referencingTrees;
        this.recordCount = recordCount;
        this.existingData = allGeneratedData;
        initDistributions();
        this.mandatoryValues = buildMandatoryValues();
    }

    @Override
    public List<Object> generate() {
        throw new UnsupportedOperationException("Markov generator is for multi-column unique keys.");
    }

    @Override
    public List<List<Object>> regenerateRows(int count) {
        if (count <= 0) return List.of();
        Set<List<Object>> localSeen = new LinkedHashSet<>();
        generateRows(count, localSeen);
        return transposeToColumns(new ArrayList<>(localSeen));
    }

    @Override
    public void generate(Map<String, List<Object>> columnData) {
        log.info("Запуск Markov генератора для {} уникальных записей и колонок {}",
                recordCount, columnsMetadata.stream().map(ColumnMetadata::getName).toList());
        StateData dummy = new StateData();
        List<List<Object>> batch = generateValues(recordCount, dummy);
        for (int i = 0; i < columnsMetadata.size(); i++) {
            columnData.put(columnsMetadata.get(i).getName(), batch.get(i));
        }
    }

    @Override
    public void generate(Map<String, List<Object>> columnData, int offset, int batchSize) {
        throw new UnsupportedOperationException("Unimplemented method 'generate'");
    }

    /**
     * Генерирует батч уникальных строк для составного ключа с взвешенным сэмплингом.
     *
     * <p>При наличии пустых батчей ({@code stateData.getEmptyBatchCount() > 0}) адаптирует
     * веса: каждые 5 пустых батчей инжектирует свежие не-MCV значения через
     * {@link #injectFreshNonMcvValues}, а при перестройке весов подавляет часто
     * используемые значения.
     *
     * @param batchSize желаемое число уникальных строк
     * @param stateData состояние: счётчик сгенерированных строк + счётчик пустых батчей
     * @return список колонок с соответствующими значениями; длина списка = числу колонок ключа
     */
    @Override
    public List<List<Object>> generateValues(int batchSize, StateData stateData) {
        int emptyBatches = stateData.getEmptyBatchCount();
        // Every N empty batches inject fresh non-MCV values (random samples from the histogram
        // range) into each non-FK column. Such values are practically guaranteed to be absent
        // from the DB so the combinations with mandatory values become insertable.
        if (emptyBatches > 0 && emptyBatches != lastEmptyBatchSeen && emptyBatches % 5 == 0) {
            injectFreshNonMcvValues(batchSize * 4);
        }
        lastEmptyBatchSeen = emptyBatches;
        this.currentEmptyBatches = emptyBatches;
        rebuildAllCumWeights();
        dirtyCount = 0;

        Set<List<Object>> localSeen = new LinkedHashSet<>();

        seedMandatoryRows(stateData.getGeneratedCount(), batchSize, mandatoryValues, localSeen);

        generateRows(batchSize, localSeen);

        if (localSeen.size() < batchSize) {
            log.warn("Не удалось получить {} уникальных строк в батче, получено {} (emptyBatches={})",
                    batchSize, localSeen.size(), emptyBatches);
        }

        stateData.advance(localSeen.size());
        return transposeToColumns(new ArrayList<>(localSeen));
    }

    private void injectFreshNonMcvValues(int countPerColumn) {
        for (int colIdx = 0; colIdx < columnsMetadata.size(); colIdx++) {
            if (columnsMetadata.get(colIdx).isForeignKey()) continue;
            ColumnMetadata colMeta = columnsMetadata.get(colIdx);
            List<Object> values = colValues.get(colIdx);
            int sizeBefore = values.size();
            Set<Object> seen = new HashSet<>(values);

            List<Object> histogram = colMeta.getHistogramm();
            ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(colMeta);
            List<Object> extra = new ArrayList<>(countPerColumn);
            if (histogram != null && !histogram.isEmpty()) {
                generator.generateValues(extra, countPerColumn, histogram.getFirst(), histogram.getLast());
            } else {
                generator.generateValues(extra, countPerColumn);
            }
            for (Object v : extra) {
                if (seen.add(v)) values.add(v);
            }
            if (values.size() == sizeBefore) continue;

            // recompute per-non-MCV weight against new pool size, keep MCV frequencies intact
            int mcvCount = colMcvCounts.get(colIdx);
            Map<Object, Double> mcv = colMeta.getMcv();
            double mcvSum = 0.0;
            for (int i = 0; i < mcvCount; i++) mcvSum += mcv.getOrDefault(values.get(i), 0.0);
            int nonMcvCount = values.size() - mcvCount;
            double perNonMcv = nonMcvCount > 0 ? Math.max(0.0, 1.0 - mcvSum) / nonMcvCount : 0.0;

            double[] newOrig = new double[values.size()];
            for (int i = 0; i < mcvCount; i++) newOrig[i] = mcv.getOrDefault(values.get(i), 0.0);
            for (int i = mcvCount; i < values.size(); i++) newOrig[i] = perNonMcv;
            colOrigWeights.set(colIdx, newOrig);

            int[] newUsed = Arrays.copyOf(colUsedCounts.get(colIdx), values.size());
            // freshly added values start with used=0 (already 0 from copyOf)
            colUsedCounts.set(colIdx, newUsed);

            Map<Object, Integer> valueIdx = colValueIndex.get(colIdx);
            for (int i = sizeBefore; i < values.size(); i++) valueIdx.put(values.get(i), i);

            log.debug("Колонка {}: добавлено {} свежих non-MCV значений (всего {})",
                    colMeta.getName(), values.size() - sizeBefore, values.size());
        }
    }

    // Адаптивный пересчёт: вес = orig * WEIGHT_DECAY^(used * stuckFactor), MCV дополнительно
    // подавляются при застревании. Так часто использованные значения проседают экспоненциально
    // по числу использований, а сила decay растёт с числом подряд пустых батчей —
    // редкие комбинации выигрывают вероятность тем быстрее, чем дольше мы топчемся на месте.
    private void rebuildAllCumWeights() {
        int emptyBatches = Math.max(0, currentEmptyBatches);
        double stuckFactor = 1.0 + emptyBatches * BOOST_PER_EMPTY;
        double mcvSuppression = emptyBatches > 0
                ? Math.pow(MCV_SUPPRESSION_BASE, Math.min(STUCK_FACTOR_EMPTY_CAP, emptyBatches))
                : 1.0;
        for (int j = 0; j < columnsMetadata.size(); j++) {
            double[] orig = colOrigWeights.get(j);
            int[] used = colUsedCounts.get(j);
            int mcvCount = colMcvCounts.get(j);
            List<Double> eff = new ArrayList<>(orig.length);
            for (int i = 0; i < orig.length; i++) {
                double w = orig[i] * Math.pow(WEIGHT_DECAY, used[i] * stuckFactor);
                if (i < mcvCount) w *= mcvSuppression;
                eff.add(w);
            }
            colCumWeights.set(j, buildCumWeights(eff));
        }
    }

    private void seedMandatoryRows(int batchStart, int batchSize,
                                   List<List<Object>> mandatoryValues,
                                   Set<List<Object>> localSeen) {
        for (int j = 0; j < columnsMetadata.size(); j++) {
            List<Object> mandatory = mandatoryValues.get(j);
            if (mandatory.isEmpty()) continue;

            int mStart = (int) ((long) mandatory.size() * batchStart / recordCount);
            int mEnd = batchStart + batchSize >= recordCount
                    ? mandatory.size()
                    : (int) ((long) mandatory.size() * (batchStart + batchSize) / recordCount);

            for (int mi = mStart; mi < mEnd; mi++) {
                List<Object> row = new ArrayList<>(columnsMetadata.size());
                for (int k = 0; k < columnsMetadata.size(); k++) {
                    row.add(k == j ? mandatory.get(mi) : sampleColumn(k));
                }
                localSeen.add(row);
            }
        }
    }

    private void generateRows(int target, Set<List<Object>> localSeen) {
        int attempts = 0;
        int maxAttempts = target * MARKOV_MAX_ATTEMPTS_PER_ITEM;
        while (localSeen.size() < target && attempts < maxAttempts) {
            Object[] rowArr = new Object[columnsMetadata.size()];
            boolean[] filled = new boolean[columnsMetadata.size()];

            for (FkGroupDist group : fkGroups) {
                List<Object> tuple = group.tuples().get(random.nextInt(group.tuples().size()));
                for (int k = 0; k < group.colIndices().size(); k++) {
                    int colIdx = group.colIndices().get(k);
                    rowArr[colIdx] = tuple.get(k);
                    filled[colIdx] = true;
                }
            }

            for (int j = 0; j < columnsMetadata.size(); j++) {
                if (!filled[j]) rowArr[j] = sampleColumn(j);
            }

            boolean added = localSeen.add(Arrays.asList(rowArr));
            if (added) {
                updateUsedCounts(rowArr, filled);
                dirtyCount++;
                if (dirtyCount >= REBUILD_INTERVAL) {
                    rebuildAllCumWeights();
                    dirtyCount = 0;
                }
            }
            attempts++;
        }
    }

    private void updateUsedCounts(Object[] rowArr, boolean[] filled) {
        for (int j = 0; j < columnsMetadata.size(); j++) {
            if (filled[j]) continue;
            Integer idx = colValueIndex.get(j).get(rowArr[j]);
            if (idx != null) colUsedCounts.get(j)[idx]++;
        }
    }

    private Object sampleColumn(int colIdx) {
        double r = random.nextDouble();
        double[] cumWeights = colCumWeights.get(colIdx);
        List<Object> values = colValues.get(colIdx);
        int idx = Arrays.binarySearch(cumWeights, r);
        if (idx < 0) idx = -(idx + 1);
        return values.get(Math.min(idx, values.size() - 1));
    }

    private void initDistributions() {
        int numCols = columnsMetadata.size();
        safeTargetPerColumn = (int) Math.ceil(
                Math.pow((double) recordCount * SPACE_SAFETY_FACTOR, 1.0 / numCols));

        colValues = new ArrayList<>(numCols);
        colCumWeights = new ArrayList<>(numCols);
        colOrigWeights = new ArrayList<>(numCols);
        colUsedCounts = new ArrayList<>(numCols);
        colValueIndex = new ArrayList<>(numCols);
        colMcvCounts = new ArrayList<>(numCols);
        dirtyCount = 0;
        for (int colIdx = 0; colIdx < numCols; colIdx++) {
            buildColDist(colIdx);
        }

        fkGroups = buildFkGroups();

        expandNonFkDistributions();

        double logSpace = colValues.stream().mapToDouble(v -> Math.log(v.size())).sum();
        if (logSpace < Math.log(recordCount)) {
            log.warn("Пространство комбинаций ({}) меньше recordCount ({}) — скорее всего из-за FK-ограничений, " +
                    "будет сгенерировано меньше строк чем запрошено",
                    (long) Math.exp(logSpace), recordCount);
        }
    }

    private List<FkGroupDist> buildFkGroups() {
        List<FkGroupDist> groups = new ArrayList<>();
        Set<Integer> assigned = new HashSet<>();

        for (int i = 0; i < columnsMetadata.size(); i++) {
            if (assigned.contains(i)) continue;
            ColumnMetadata col = columnsMetadata.get(i);
            if (!col.isForeignKey()) continue;
            List<List<String>> peers = col.getCompositeForeignPeers();
            if (peers == null || peers.isEmpty() || peers.getFirst().isEmpty()) continue;

            List<String> peerGroup = peers.getFirst();
            List<Integer> groupIndices = new ArrayList<>();
            for (int j = 0; j < columnsMetadata.size(); j++) {
                String name = columnsMetadata.get(j).getName();
                for (String peer : peerGroup) {
                    if (peer.endsWith("." + name)) {
                        groupIndices.add(j);
                        break;
                    }
                }
            }

            if (groupIndices.size() > 1) {
                assigned.addAll(groupIndices);
                List<List<Object>> tuples = buildFkGroupTuples(groupIndices);
                if (!tuples.isEmpty()) {
                    groups.add(new FkGroupDist(groupIndices, tuples));
                    log.debug("FK группа: колонки {}, {} уникальных кортежей",
                            groupIndices.stream().map(k -> columnsMetadata.get(k).getName()).toList(),
                            tuples.size());
                }
            }
        }
        return groups;
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

        int rowCount = parentColumnData.get(0).size();
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

    private void buildColDist(int colIdx) {
        ColumnMetadata colMeta = columnsMetadata.get(colIdx);
        Map<Object, Double> mcv = colMeta.getMcv();

        List<Object> values = new ArrayList<>();
        List<Double> weights = new ArrayList<>();
        for (Map.Entry<Object, Double> e : mcv.entrySet()) {
            values.add(e.getKey());
            weights.add(e.getValue());
        }
        double mcvSum = weights.stream().mapToDouble(Double::doubleValue).sum();
        double remaining = Math.max(0.0, 1.0 - mcvSum);
        colMcvCounts.add(values.size());

        Set<Object> seen = new HashSet<>(mcv.keySet());
        List<Object> nonMcvCandidates = new ArrayList<>();

        if (colMeta.isForeignKey()) {
            addFkCandidates(colMeta, seen, nonMcvCandidates);
        } else {
            addGeneratedCandidates(colMeta, seen, nonMcvCandidates);
        }

        if (!nonMcvCandidates.isEmpty()) {
            double perCandidate = remaining / nonMcvCandidates.size();
            values.addAll(nonMcvCandidates);
            for (int i = 0; i < nonMcvCandidates.size(); i++) {
                weights.add(perCandidate);
            }
        }

        colValues.add(values);
        colCumWeights.add(buildCumWeights(weights));

        double[] origW = weights.stream().mapToDouble(Double::doubleValue).toArray();
        colOrigWeights.add(origW);
        colUsedCounts.add(new int[origW.length]);

        Map<Object, Integer> valueIdx = new HashMap<>();
        for (int i = 0; i < values.size(); i++) valueIdx.put(values.get(i), i);
        colValueIndex.add(valueIdx);

        log.debug("Колонка {}: {} значений (mcv={}, extra={})",
                colMeta.getName(), values.size(), mcv.size(), nonMcvCandidates.size());
    }

    private List<List<Object>>  buildMandatoryValues() {
        List<List<Object>> result = new ArrayList<>(columnsMetadata.size());
        for (ColumnMetadata colMeta : columnsMetadata) {
            Set<Object> seen = new HashSet<>();
            List<Object> mandatory = new ArrayList<>();
            List<ReferencingTreeNode> trees = referencingTrees != null
                    ? referencingTrees.get(colMeta.getName())
                    : null;
            if (trees != null) {
                for (ReferencingTreeNode node : trees) {
                    collectReferencedValues(seen, mandatory, node);
                }
            }
            result.add(mandatory);
        }
        return result;
    }

    private void addFkCandidates(ColumnMetadata colMeta, Set<Object> seen, List<Object> candidates) {
        if (colMeta.getCompositeForeignPeers() != null && !colMeta.getCompositeForeignPeers().isEmpty()) {
            List<String> peerColumns = colMeta.getCompositeForeignPeers().getFirst().stream().toList();
            if (!peerColumns.isEmpty()) {
                int selfPos = -1;
                for (int i = 0; i < peerColumns.size(); i++) {
                    if (peerColumns.get(i).endsWith("." + colMeta.getName())) {
                        selfPos = i;
                        break;
                    }
                }
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
                        for (Object val : parentColumnsData.get(selfPos)) {
                            if (seen.add(val)) candidates.add(val);
                        }
                        return;
                    }
                }
            }
        }

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
            List<Object> shuffled = new ArrayList<>(intersection);
            Collections.shuffle(shuffled, random);
            for (Object val : shuffled) {
                if (seen.add(val)) candidates.add(val);
            }
        }
    }

    private void addGeneratedCandidates(ColumnMetadata colMeta, Set<Object> seen, List<Object> candidates) {
        double nd = colMeta.getNdistinct();
        int statsTarget = (int) (nd < 0 ? -nd * recordCount : nd);
        int target = Math.max((int) (statsTarget * 1.2), safeTargetPerColumn);
        int toAdd = Math.max(0, target - seen.size());
        if (toAdd == 0) return;

        List<Object> histogram = colMeta.getHistogramm();
        ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(colMeta);
        List<Object> extra = new ArrayList<>(toAdd);
        if (histogram != null && !histogram.isEmpty()) {
            generator.generateValues(extra, toAdd, histogram.getFirst(), histogram.getLast());
        } else {
            generator.generateValues(extra, toAdd);
        }
        for (Object v : extra) {
            if (seen.add(v)) candidates.add(v);
        }
        int safety = toAdd * 10;
        int att = 0;
        while (candidates.size() < toAdd && att < safety) {
            att++;
            Object v = generator.generateValue();
            if (seen.add(v)) candidates.add(v);
        }
    }

    private void expandNonFkDistributions() {
        for (int colIdx = 0; colIdx < columnsMetadata.size(); colIdx++) {
            if (columnsMetadata.get(colIdx).isForeignKey()) continue;

            ColumnMetadata colMeta = columnsMetadata.get(colIdx);
            List<Object> values = colValues.get(colIdx);
            int sizeBefore = values.size();
            Set<Object> seen = new HashSet<>(values);
            int toAdd = values.size();

            List<Object> histogram = colMeta.getHistogramm();
            ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(colMeta);
            List<Object> extra = new ArrayList<>(toAdd);
            if (histogram != null && !histogram.isEmpty()) {
                generator.generateValues(extra, toAdd, histogram.getFirst(), histogram.getLast());
            } else {
                generator.generateValues(extra, toAdd);
            }
            for (Object v : extra) {
                if (seen.add(v)) values.add(v);
            }

            // Rebuild weights: MCV values keep original frequencies, new non-MCV share the remainder
            int mcvCount = colMcvCounts.get(colIdx);
            Map<Object, Double> mcv = colMeta.getMcv();
            List<Double> weights = new ArrayList<>(values.size());
            double mcvSum = 0.0;
            for (int i = 0; i < mcvCount; i++) {
                double w = mcv.getOrDefault(values.get(i), 0.0);
                weights.add(w);
                mcvSum += w;
            }
            double remaining = Math.max(0.0, 1.0 - mcvSum);
            int nonMcvCount = values.size() - mcvCount;
            double perNonMcv = nonMcvCount > 0 ? remaining / nonMcvCount : 0.0;
            for (int i = mcvCount; i < values.size(); i++) {
                weights.add(perNonMcv);
            }
            colCumWeights.set(colIdx, buildCumWeights(weights));

            double[] newOrig = weights.stream().mapToDouble(Double::doubleValue).toArray();
            colOrigWeights.set(colIdx, newOrig);

            int[] newUsed = Arrays.copyOf(colUsedCounts.get(colIdx), values.size());
            colUsedCounts.set(colIdx, newUsed);

            Map<Object, Integer> valueIdx = colValueIndex.get(colIdx);
            for (int i = sizeBefore; i < values.size(); i++) {
                valueIdx.put(values.get(i), i);
            }

            dirtyCount = 0;
            log.debug("Расширена колонка {}: теперь {} уникальных значений",
                    colMeta.getName(), values.size());
        }
    }

    private double[] buildCumWeights(List<Double> weights) {
        double[] cumWeights = new double[weights.size()];
        double cum = 0.0;
        for (int i = 0; i < weights.size(); i++) {
            cum += weights.get(i);
            cumWeights[i] = cum;
        }
        if (cumWeights.length > 0 && cum > 0.0) {
            for (int i = 0; i < cumWeights.length; i++) {
                cumWeights[i] /= cum;
            }
        }
        if (cumWeights.length > 0) cumWeights[cumWeights.length - 1] = 1.0;
        return cumWeights;
    }

    private List<List<Object>> transposeToColumns(List<List<Object>> rows) {
        List<List<Object>> cols = new ArrayList<>(columnsMetadata.size());
        for (int j = 0; j < columnsMetadata.size(); j++) {
            cols.add(new ArrayList<>(rows.size()));
        }
        for (List<Object> row : rows) {
            for (int j = 0; j < row.size(); j++) {
                cols.get(j).add(row.get(j));
            }
        }
        return cols;
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