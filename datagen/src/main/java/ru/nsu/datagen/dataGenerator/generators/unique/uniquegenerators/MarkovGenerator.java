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

@Slf4j
public class MarkovGenerator implements UniqueKeyGenerator {
    private static final int MARKOV_MAX_ATTEMPTS_PER_ITEM = 200;
    private static final double SPACE_SAFETY_FACTOR = 10.0;

    private final List<ColumnMetadata> columnsMetadata;
    private final int recordCount;
    private final Map<String, List<ReferencingTreeNode>> referencingTrees;
    private final Map<String, List<Object>> existingData;
    private final Random random = new Random();

    private List<List<Object>> colValues;
    private List<double[]> colCumWeights;
    private List<Integer> colMcvCounts;
    private int safeTargetPerColumn;
    private List<List<Object>> mandatoryValues;

    private record FkGroupDist(List<Integer> colIndices, List<List<Object>> tuples) {}
    private List<FkGroupDist> fkGroups = Collections.emptyList();

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

    @Override
    public List<List<Object>> generateValues(int batchSize, StateData stateData) {
        Set<List<Object>> localSeen = new LinkedHashSet<>();

        seedMandatoryRows(stateData.getGeneratedCount(), batchSize, mandatoryValues, localSeen);

        generateRows(batchSize, localSeen);

        if (localSeen.size() < batchSize) {
            log.debug("Пространство комбинаций исчерпано ({}/{}), расширяем non-FK колонки",
                    localSeen.size(), batchSize);
            expandNonFkDistributions();
            generateRows(batchSize, localSeen);
            if (localSeen.size() < batchSize) {
                log.warn("Не удалось получить {} уникальных строк в батче, получено {}",
                        batchSize, localSeen.size());
            }
        }

        stateData.advance(localSeen.size());
        return transposeToColumns(new ArrayList<>(localSeen));
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

            localSeen.add(Arrays.asList(rowArr));
            attempts++;
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

    // --- Distribution initialization ---

    private void initDistributions() {
        int numCols = columnsMetadata.size();
        safeTargetPerColumn = (int) Math.ceil(
                Math.pow((double) recordCount * SPACE_SAFETY_FACTOR, 1.0 / numCols));

        colValues = new ArrayList<>(numCols);
        colCumWeights = new ArrayList<>(numCols);
        colMcvCounts = new ArrayList<>(numCols);
        for (int colIdx = 0; colIdx < numCols; colIdx++) {
            buildColDist(colIdx);
        }

        fkGroups = buildFkGroups();

        double logSpace = colValues.stream().mapToDouble(v -> Math.log(v.size())).sum();
        if (logSpace < Math.log(recordCount)) {
            log.warn("Пространство комбинаций ({}) меньше recordCount ({}), скорее всего из-за FK-ограничений — " +
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