package ru.nsu.datagen.dataGenerator.generators.fk.impl;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.ComplexForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.*;
import java.util.concurrent.ThreadLocalRandom;

@Slf4j
public class OneToManyForeignKeyGenerator implements ForeignKeyGenerator, ComplexForeignKeyGenerator {
    private final ThreadLocalRandom random = ThreadLocalRandom.current();
    
    public List<Object> generateForeignKeys(ColumnMetadata columnMetadata,
                                                  Map<String, List<Object>> allGeneratedData) {
        log.info("Generating database 1-M foreign keys for columns {}", columnMetadata.getName());
        String refSchema = columnMetadata.getForeignKeyMetadata().getFirst().getReferencedSchema();
        String refTable = columnMetadata.getForeignKeyMetadata().getFirst().getReferencedTable();
        String refColumn = columnMetadata.getForeignKeyMetadata().getFirst().getReferencedColumn();
        String refKey = refSchema + '.' + refTable + '.' + refColumn;
        Set<Object> usefulData = new HashSet<>(allGeneratedData.get(refKey));

        for (int i = 1; i < columnMetadata.getForeignKeyMetadata().size(); i++) {
            refSchema = columnMetadata.getForeignKeyMetadata().get(i).getReferencedSchema();
            refTable = columnMetadata.getForeignKeyMetadata().get(i).getReferencedTable();
            refColumn = columnMetadata.getForeignKeyMetadata().get(i).getReferencedColumn();
            usefulData.retainAll(allGeneratedData.get(refSchema + '.' + refTable + '.' + refColumn));
        }

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

        double nullPercentage = columnMetadata.getNullPercentage();
        for (int i = 0; i < recordCount * (nullPercentage/100.0); i++) {
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
    public List<List<Object>> generateForeignKeys(List<ColumnMetadata> columnMetadata, Map<String, List<Object>> allGeneratedData) {
        int recordCount = columnMetadata.getFirst().getRecordCount();
        int parentTableCount = columnMetadata.getFirst().getForeignKeyMetadata().size();

        var firstFk = columnMetadata.getFirst().getForeignKeyMetadata().getFirst();
        int parentSize = allGeneratedData.get(
                firstFk.getReferencedSchema() + "." + firstFk.getReferencedTable() + "." + firstFk.getReferencedColumn()
        ).size();

        Set<List<Object>> intersection = new HashSet<>();
        for (int i = 0; i < parentSize; i++) {
            List<Object> data = new ArrayList<>(columnMetadata.size());
            for (ColumnMetadata column : columnMetadata) {
                var fk = column.getForeignKeyMetadata().getFirst();
                String refSchema = fk.getReferencedSchema();
                String refTable = fk.getReferencedTable();
                String refColumn = fk.getReferencedColumn();
                String refKey = refSchema + '.' + refTable + '.' + refColumn;
                data.add(allGeneratedData.get(refKey).get(i));
            }
            intersection.add(data);
        }

        for (int i = 1; i < parentTableCount; i++) {
            var fk = columnMetadata.getFirst().getForeignKeyMetadata().get(i);
            int psize =  allGeneratedData.get(fk.getReferencedSchema() + "." +  fk.getReferencedTable() + "." + fk.getReferencedColumn()).size();

            Set<List<Object>> currentSet = new HashSet<>();
            for (int j = 0; j < psize; j++) {
                List<Object> data = new ArrayList<>(columnMetadata.size());
                for (ColumnMetadata column : columnMetadata) {
                    var currentFk = column.getForeignKeyMetadata().getFirst();
                    String currentRefSchema = currentFk.getReferencedSchema();
                    String currentRefTable = currentFk.getReferencedTable();
                    String currentRefColumn = currentFk.getReferencedColumn();
                    String currentRefKey = currentRefSchema + '.' + currentRefTable + '.' + currentRefColumn;
                    data.add(allGeneratedData.get(currentRefKey).get(j));
                }
                currentSet.add(data);
            }
            intersection.retainAll(currentSet);
        }

        if (intersection.isEmpty()) {
            throw new RuntimeException("Пересечение кортежей для composite FK пусто");
        }
        List<List<Object>> validTuples = new ArrayList<>(intersection);

        List<List<Object>> result = new ArrayList<>(columnMetadata.size());
        for (int j = 0; j < columnMetadata.size(); j++) {
            result.add(new ArrayList<>(recordCount));
        }
        for (int i = 0; i < recordCount; i++) {
            List<Object> tuple = validTuples.get(random.nextInt(validTuples.size()));
            for (int j = 0; j < columnMetadata.size(); j++) {
                result.get(j).add(tuple.get(j));
            }
        }
        return result;
    }
}