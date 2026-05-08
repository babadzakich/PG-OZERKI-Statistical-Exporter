package ru.nsu.datagen.dataGenerator.generators.fk.impl;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ThreadLocalRandom;
import java.util.stream.Collectors;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.ComplexForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

@Slf4j
public class OneToManyForeignKeyGenerator implements ForeignKeyGenerator, ComplexForeignKeyGenerator {
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
            int psize =  allGeneratedData.get(fk.getReferencedSchema() + "." +  fk.getReferencedTable() + "." + fk.getReferencedColumn()).size();

            List<List<Object>> currentSet = new ArrayList<>();
            for (int j = 0; j < psize; j++) {
                List<Object> data = new ArrayList<>(columnMetadatas.size());
                for (ColumnMetadata column : columnMetadatas) {
                    var currentFk = column.getForeignKeyMetadata().get(i);
                    String currentRefSchema = currentFk.getReferencedSchema();
                    String currentRefTable = currentFk.getReferencedTable();
                    String currentRefColumn = currentFk.getReferencedColumn();
                    String currentRefKey = currentRefSchema + '.' + currentRefTable + '.' + currentRefColumn;
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
        int offset = stateData.getGeneratedCount();
        log.info("Generating batch of {} values from {} for column: {}", batchSize, offset, columnMetadatas.stream().map(ColumnMetadata::getName).collect(Collectors.toList()));
        List<List<Object>> result = new ArrayList<>();
        for (int i = 0; i < columnMetadatas.size(); i++) {
            result.add(new ArrayList<>());
        }

        int remaining = columnMetadatas.getFirst().getRecordCount() - offset;
        int toGenerate = Math.min(batchSize, Math.max(0, remaining));
        for (int i = 0; i < toGenerate; i++) {
            if (columnMetadatas.size() > 1) {
                List<Object> tuple = intersection.get(random.nextInt(intersection.size()));
                for (int j = 0; j < columnMetadatas.size(); j++) {
                    result.get(j).add(tuple.get(j));
                }
            } else {
                result.getFirst().add(usefulData.get(random.nextInt(usefulData.size())));
            }
        }

        stateData.advance(toGenerate);
        return result;
    }

    @Override
    public List<Object> generateSimpleForeignKeys(Map<String, List<Object>> allGeneratedData) {
        ColumnMetadata columnMetadata = columnMetadatas.getFirst();
        log.info("Generating database 1-M foreign keys for columns {}", columnMetadata.getName());
        
        

        int recordCount = columnMetadata.getRecordCount();
        List<Object> foreignKeys = new ArrayList<>();

        columnMetadata.getMcv().forEach((mcv, freq) -> {
            usefulData.remove(mcv);
            long mvcCount = Math.round(freq * columnMetadata.getRecordCount());
            for (long j = 0; j < mvcCount; j++) {
                foreignKeys.add(mcv);
            }
            log.trace("Adding MVC value: {} with frequency: {} resulting in count: {}", mcv, freq, mvcCount);
        });

        int nullPercentage = columnMetadata.getNullCount();
        for (int i = 0; i < nullPercentage; i++) {
            foreignKeys.add(null);
        }

        if (nullPercentage > 0) {
            usefulData.remove(null);
        }

        List<Object> refValues = new ArrayList<>(usefulData);

        for (int i = foreignKeys.size(); i < recordCount; i++) {
            foreignKeys.add(refValues.get(random.nextInt(refValues.size())));
        }

        return foreignKeys;
    }

    @Override
    public List<List<Object>> generateComplexForeignKeys(Map<String, List<Object>> allGeneratedData) {
        int recordCount = columnMetadatas.getFirst().getRecordCount();
        
        List<List<Object>> validTuples = new ArrayList<>(this.intersection);

        List<List<Object>> result = new ArrayList<>(columnMetadatas.size());
        for (int j = 0; j < columnMetadatas.size(); j++) {
            result.add(new ArrayList<>(recordCount));
        }
        for (int i = 0; i < recordCount; i++) {
            List<Object> tuple = validTuples.get(random.nextInt(validTuples.size()));
            for (int j = 0; j < columnMetadatas.size(); j++) {
                result.get(j).add(tuple.get(j));
            }
        }
        return result;
    }
}
