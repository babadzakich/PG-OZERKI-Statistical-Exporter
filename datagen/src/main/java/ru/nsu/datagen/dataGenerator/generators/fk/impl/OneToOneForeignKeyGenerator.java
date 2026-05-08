package ru.nsu.datagen.dataGenerator.generators.fk.impl;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ThreadLocalRandom;
import java.util.stream.Collectors;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.ComplexForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

@Slf4j
public class OneToOneForeignKeyGenerator implements ForeignKeyGenerator, ComplexForeignKeyGenerator {
    private final ThreadLocalRandom random = ThreadLocalRandom.current();
    private final List<ColumnMetadata> columnMetadatas;
    private final List<List<Object>> intersection;
    private final List<Object> usefulData;

    public OneToOneForeignKeyGenerator(ColumnMetadata columnMetadata, Map<String, List<Object>> allGeneratedData) {
        this.columnMetadatas = List.of(columnMetadata);
        this.usefulData = buildSimpleIntersection(columnMetadata, allGeneratedData);
        this.intersection = usefulData.stream().map(List::of).collect(Collectors.toList());

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

    public OneToOneForeignKeyGenerator(List<ColumnMetadata> columnMetadatas) {
        this.columnMetadatas = columnMetadatas;
        this.usefulData = null;
        this.intersection = List.of();
    }

    @Override
    public List<List<Object>> generateValues(int batchSize, StateData stateData) {
        int offset = stateData.getGeneratedCount();
        log.info(
                "Generating batch of {} values from {} for columns: {}",
                batchSize,
                offset,
                columnMetadatas.stream().map(ColumnMetadata::getName).collect(Collectors.toList())
        );

        List<List<Object>> result = new ArrayList<>();
        for (int i = 0; i < columnMetadatas.size(); i++) {
            result.add(new ArrayList<>());
        }

        int remaining = columnMetadatas.getFirst().getRecordCount() - offset;
        int toGenerate = Math.min(batchSize, Math.max(0, remaining));
        if (columnMetadatas.size() > 1) {
            int end = Math.min(offset + toGenerate, intersection.size());
            for (int i = offset; i < end; i++) {
                List<Object> tuple = intersection.get(i);
                for (int j = 0; j < columnMetadatas.size(); j++) {
                    result.get(j).add(tuple.get(j));
                }
            }
            stateData.advance(Math.max(0, end - offset));
        } else {
            int end = Math.min(offset + toGenerate, usefulData.size());
            for (int i = offset; i < end; i++) {
                result.getFirst().add(usefulData.get(i));
            }
            stateData.advance(Math.max(0, end - offset));
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

    @Override
    public List<Object> generateSimpleForeignKeys(Map<String, List<Object>> allGeneratedData) {
        ColumnMetadata columnMetadata = columnMetadatas.getFirst();
        log.info("Generating database 1-1 foreign keys for column {}", columnMetadata.getName());

        int recordCount = columnMetadata.getRecordCount();
        List<Object> foreignKeys = new ArrayList<>();
        List<Object> availableValues = new ArrayList<>(usefulData);

        columnMetadata.getMcv().forEach((mcv, freq) -> {
            long mcvCount = Math.round(freq * columnMetadata.getRecordCount());
            if (mcvCount > 1) {
                throw new RuntimeException("MCV value duplicates are not allowed for 1-1 relationship: " + mcv);
            }
            if (mcvCount == 1) {
                if (!availableValues.remove(mcv)) {
                    throw new RuntimeException("MCV value is not present in referenced values for 1-1 FK: " + mcv);
                }
                foreignKeys.add(mcv);
            }
            log.trace("Adding MCV value: {} with frequency: {} resulting in count: {}", mcv, freq, mcvCount);
        });

        for (int i = 0; i < columnMetadata.getNullCount(); i++) {
            foreignKeys.add(null);
        }
        availableValues.remove(null);

        int requiredValues = recordCount - foreignKeys.size();
        if (availableValues.size() < requiredValues) {
            throw new RuntimeException("Not enough unique referenced values for 1-1 relationship. Needed: "
                    + requiredValues + ", available: " + availableValues.size());
        }

        Collections.shuffle(availableValues, random);
        foreignKeys.addAll(availableValues.subList(0, requiredValues));
        return foreignKeys;
    }

    @Override
    public List<List<Object>> generateComplexForeignKeys(Map<String, List<Object>> allGeneratedData) {
        int recordCount = columnMetadatas.getFirst().getRecordCount();
        List<List<Object>> validTuples = new ArrayList<>(this.intersection);

        if (validTuples.size() < recordCount) {
            throw new RuntimeException("Not enough unique referenced tuples for 1-1 relationship. Needed: "
                    + recordCount + ", available: " + validTuples.size());
        }

        Collections.shuffle(validTuples, random);
        validTuples = validTuples.subList(0, recordCount);

        List<List<Object>> result = new ArrayList<>(columnMetadatas.size());
        for (int j = 0; j < columnMetadatas.size(); j++) {
            result.add(new ArrayList<>(recordCount));
        }
        for (List<Object> tuple : validTuples) {
            for (int j = 0; j < columnMetadatas.size(); j++) {
                result.get(j).add(tuple.get(j));
            }
        }
        return result;
    }
}
