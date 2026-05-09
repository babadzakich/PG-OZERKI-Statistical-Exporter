package ru.nsu.datagen.dataGenerator.generators.fk.impl;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ThreadLocalRandom;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.ColumnGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

@Slf4j
public class OneToOneForeignKeyGenerator implements ColumnGenerator {
    private final ThreadLocalRandom random = ThreadLocalRandom.current();
    private final List<ColumnMetadata> columnMetadatas;
    private List<List<Object>> intersection;
    private final List<Object> usefulData;

    public OneToOneForeignKeyGenerator(ColumnMetadata columnMetadata, Map<String, List<Object>> allGeneratedData) {
        this.columnMetadatas = List.of(columnMetadata);
        this.usefulData = buildSimpleIntersection(columnMetadata, allGeneratedData);
        this.intersection = usefulData.stream().map(List::of).collect(java.util.stream.Collectors.toList());

        int nonNullCount = columnMetadata.getRecordCount() - columnMetadata.getNullCount();
        if (this.usefulData.size() < nonNullCount) {
            throw new RuntimeException("Not enough unique referenced values for 1-1 relationship. Needed: "
                    + nonNullCount + ", available: " + this.usefulData.size());
        }
    }

    public OneToOneForeignKeyGenerator(List<ColumnMetadata> columnMetadatas, Map<String, List<Object>> allGeneratedData) {
        this.columnMetadatas = columnMetadatas;
        this.usefulData = null;
        this.intersection = buildComplexIntersection(columnMetadatas, allGeneratedData);

        int recordCount = columnMetadatas.getFirst().getRecordCount();
        if (this.intersection.size() < recordCount) {
            throw new RuntimeException("Not enough unique referenced tuples for 1-1 relationship. Needed: "
                    + recordCount + ", available: " + this.intersection.size());
        }
    }

    @Override
    public List<List<Object>> generateValues(int batchSize, StateData stateData) {
        if (columnMetadatas.size() == 1) {
            return List.of(generateSimple(batchSize, stateData));
        }
        return generateComplex(batchSize);
    }

    private List<Object> generateSimple(int batchSize, StateData stateData) {
        ColumnMetadata columnMetadata = columnMetadatas.getFirst();
        log.info("Generating 1-1 foreign keys for column {}", columnMetadata.getName());
        List<Object> data = new ArrayList<>();
        for (int i = 0; i < batchSize; i++) {
            double r = random.nextDouble();
            if (r < stateData.getNullChance()) {
                data.add(null);
                stateData.dropNull();
            }
            else {
                data.add(generateNonNull(stateData, r));
            }
        }
        return data;
    }

    private Object generateNonNull(StateData stateData, double r) {
        for (int j = 0; j < stateData.getMcvValues().size(); j++) {
            if (r < stateData.getMcvChances().get(j)) {
                stateData.getMcvChances().remove(j);
                return stateData.getMcvValues().remove(j);
            }
        }
        int index = random.nextInt(usefulData.size());
        return usefulData.remove(index);
    }

    private List<List<Object>> generateComplex(int batchSize) {
        int recordCount = columnMetadatas.getFirst().getRecordCount();
        List<List<Object>> validTuples = intersection.subList(0, batchSize);

        if (validTuples.size() < recordCount) {
            throw new RuntimeException("Not enough unique referenced tuples for 1-1 relationship. Needed: "
                    + recordCount + ", available: " + validTuples.size());
        }

        Collections.shuffle(validTuples, random);
        intersection = intersection.subList(batchSize, intersection.size());

        List<List<Object>> result = new ArrayList<>(columnMetadatas.size());
        for (int j = 0; j < columnMetadatas.size(); j++) {
            result.add(new ArrayList<>(batchSize));
        }
        for (List<Object> tuple : validTuples) {
            for (int j = 0; j < columnMetadatas.size(); j++) {
                result.get(j).add(tuple.get(j));
            }
        }
        return result;
    }

    private static List<Object> buildSimpleIntersection(
            ColumnMetadata columnMetadata,
            Map<String, List<Object>> allGeneratedData) {
        Set<Object> uniqueValues = new LinkedHashSet<>();

        for (int i = 0; i < columnMetadata.getForeignKeyMetadata().size(); i++) {
            var fk = columnMetadata.getForeignKeyMetadata().get(i);
            String refKey = fk.getReferencedSchema() + '.' + fk.getReferencedTable() + '.' + fk.getReferencedColumn();
            if (i == 0) {
                uniqueValues.addAll(allGeneratedData.get(refKey));
            } else {
                uniqueValues.retainAll(allGeneratedData.get(refKey));
            }
        }

        return new ArrayList<>(uniqueValues);
    }

    private static List<List<Object>> buildComplexIntersection(
            List<ColumnMetadata> columnMetadatas,
            Map<String, List<Object>> allGeneratedData) {
        int parentTableCount = columnMetadatas.getFirst().getForeignKeyMetadata().size();

        var firstFk = columnMetadatas.getFirst().getForeignKeyMetadata().getFirst();
        int parentSize = allGeneratedData.get(
                firstFk.getReferencedSchema() + "." + firstFk.getReferencedTable() + "." + firstFk.getReferencedColumn()
        ).size();

        Set<List<Object>> intersection = new LinkedHashSet<>();
        for (int i = 0; i < parentSize; i++) {
            intersection.add(readTuple(columnMetadatas, allGeneratedData, 0, i));
        }

        for (int tableIndex = 1; tableIndex < parentTableCount; tableIndex++) {
            var fk = columnMetadatas.getFirst().getForeignKeyMetadata().get(tableIndex);
            int parentDataSize = allGeneratedData.get(
                    fk.getReferencedSchema() + "." + fk.getReferencedTable() + "." + fk.getReferencedColumn()
            ).size();

            Set<List<Object>> currentSet = new LinkedHashSet<>();
            for (int rowIndex = 0; rowIndex < parentDataSize; rowIndex++) {
                currentSet.add(readTuple(columnMetadatas, allGeneratedData, tableIndex, rowIndex));
            }
            intersection.retainAll(currentSet);
        }

        return new ArrayList<>(intersection);
    }

    private static List<Object> readTuple(
            List<ColumnMetadata> columnMetadatas,
            Map<String, List<Object>> allGeneratedData,
            int tableIndex,
            int rowIndex) {
        List<Object> tuple = new ArrayList<>(columnMetadatas.size());
        for (ColumnMetadata column : columnMetadatas) {
            var fk = column.getForeignKeyMetadata().get(tableIndex);
            String refKey = fk.getReferencedSchema() + "." + fk.getReferencedTable() + "." + fk.getReferencedColumn();
            tuple.add(allGeneratedData.get(refKey).get(rowIndex));
        }
        return tuple;
    }
}