package ru.nsu.datagen.dataGenerator.generators.fk.impl;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ThreadLocalRandom;
import java.util.stream.Collectors;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.ColumnGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

@Slf4j
public class OneToManyForeignKeyGenerator implements ColumnGenerator {
    private final ThreadLocalRandom random = ThreadLocalRandom.current();
    private final List<ColumnMetadata> columnMetadatas;
    private final List<List<Object>> intersection;
    private final List<Object> usefulData;

    public OneToManyForeignKeyGenerator(ColumnMetadata columnMetadata, Map<String, List<Object>> allGeneratedData) {
        this.columnMetadatas = List.of(columnMetadata);
        this.usefulData = new ArrayList<>();

        for (int i = 0; i < columnMetadata.getForeignKeyMetadata().size(); i++) {
            String refSchema = columnMetadata.getForeignKeyMetadata().get(i).getReferencedSchema();
            String refTable = columnMetadata.getForeignKeyMetadata().get(i).getReferencedTable();
            String refColumn = columnMetadata.getForeignKeyMetadata().get(i).getReferencedColumn();
            String refKey = refSchema + '.' + refTable + '.' + refColumn;
            if (i == 0) {
                usefulData.addAll(allGeneratedData.get(refKey));
            } else {
                usefulData.retainAll(allGeneratedData.get(refKey));
            }
        }
        this.intersection = usefulData.stream().map(List::of).collect(Collectors.toList());
    }

    public OneToManyForeignKeyGenerator(List<ColumnMetadata> columnMetadatas, Map<String, List<Object>> allGeneratedData) {
        this.columnMetadatas = columnMetadatas;
        this.usefulData = null;
        int parentTableCount = columnMetadatas.getFirst().getForeignKeyMetadata().size();

        var firstFk = columnMetadatas.getFirst().getForeignKeyMetadata().getFirst();
        int parentSize = allGeneratedData.get(
                firstFk.getReferencedSchema() + "." + firstFk.getReferencedTable() + "." + firstFk.getReferencedColumn()
        ).size();

        this.intersection = new ArrayList<>();
        for (int i = 0; i < parentSize; i++) {
            List<Object> data = new ArrayList<>(columnMetadatas.size());
            for (ColumnMetadata column : columnMetadatas) {
                var fk = column.getForeignKeyMetadata().getFirst();
                String refSchema = fk.getReferencedSchema();
                String refTable = fk.getReferencedTable();
                String refColumn = fk.getReferencedColumn();
                String refKey = refSchema + '.' + refTable + '.' + refColumn;
                data.add(allGeneratedData.get(refKey).get(i));
            }
            this.intersection.add(data);
        }

        for (int i = 1; i < parentTableCount; i++) {
            var fk = columnMetadatas.getFirst().getForeignKeyMetadata().get(i);
            int psize = allGeneratedData.get(fk.getReferencedSchema() + "." + fk.getReferencedTable() + "." + fk.getReferencedColumn()).size();

            List<List<Object>> currentSet = new ArrayList<>();
            for (int j = 0; j < psize; j++) {
                List<Object> data = new ArrayList<>(columnMetadatas.size());
                for (ColumnMetadata column : columnMetadatas) {
                    var currentFk = column.getForeignKeyMetadata().get(i);
                    String currentRefKey = currentFk.getReferencedSchema() + '.' + currentFk.getReferencedTable() + '.' + currentFk.getReferencedColumn();
                    data.add(allGeneratedData.get(currentRefKey).get(j));
                }
                currentSet.add(data);
            }
            this.intersection.retainAll(currentSet);
        }

        if (this.intersection.isEmpty()) {
            throw new RuntimeException("Пересечение кортежей для composite FK пусто");
        }
    }

    @Override
    public List<List<Object>> generateValues(int batchSize, StateData stateData) {
        if (columnMetadatas.size() == 1) {
            return List.of(generateSimple(batchSize, stateData));
        }
        return generateComplex(batchSize, stateData);
    }

    private List<Object> generateSimple(int batchSize, StateData stateData) {
        ColumnMetadata columnMetadata = columnMetadatas.getFirst();
        log.info("Generating 1-M foreign keys for column {}", columnMetadata.getName());
        List<Object> data = new ArrayList<>();
        for (int i = 0; i < batchSize; i++) {
            double r = random.nextDouble();
            if (r < stateData.getNullChances()[0])
                data.add(null);
            else {
                data.add(generateNonNull(stateData, r));
            }
        }
        return data;
    }

    private Object generateNonNull(StateData stateData, double r) {
        for (int j = 0; j < stateData.getMcvChances().getFirst().chances().size(); j++) {
            if (r < stateData.getMcvChances().getFirst().chances().get(j)) {
                return stateData.getMcvChances().getFirst().values().get(j);
            }
        }
        return usefulData.get(random.nextInt(usefulData.size()));
    }

    private List<List<Object>> generateComplex(int batchSize, StateData stateData) {
        int recordCount = columnMetadatas.getFirst().getRecordCount();
        List<List<Object>> result = new ArrayList<>(columnMetadatas.size());
        for (int j = 0; j < columnMetadatas.size(); j++) {
            result.add(new ArrayList<>(recordCount));
        }
        for (int i = 0; i < batchSize; i++) {
            List<Object> tuple = intersection.get(random.nextInt(intersection.size()));
            for (int j = 0; j < columnMetadatas.size(); j++) {
                result.get(j).add(tuple.get(j));
            }
        }
        return result;
    }
}