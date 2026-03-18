package ru.nsu.datagen.dataGenerator.generators.fk.impl;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.ComplexForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.*;
import java.util.concurrent.ThreadLocalRandom;
import java.util.stream.Collectors;
@Slf4j
public class OneToOneForeignKeyGenerator implements ForeignKeyGenerator, ComplexForeignKeyGenerator {
    private final ThreadLocalRandom random = ThreadLocalRandom.current();

    @Override
    public List<Object> generateForeignKeys(ColumnMetadata columnMetadata,
                                            Map<String, List<Object>> referencedData,
                                            Map<String, List<Object>> allGeneratedData) {

        log.info("Generating database 1-1 foreign keys for column {}", columnMetadata.getName());
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
        if (usefulData.size() < recordCount) {
            log.error("Not enough unique referenced values for 1-1 relationship. Needed: {}, available: {}. " +
                    "Some values will be duplicated.", recordCount, usefulData.size());
            throw new RuntimeException("Not enough unique referenced values for 1-1 relationship.");
        }

        return usefulData.stream()
                .collect(Collectors.collectingAndThen(Collectors.toList(), list -> {
                    Collections.shuffle(list, random);
                    return list.subList(0, recordCount);
                }));
    }

    @Override
    public List<List<Object>> generateForeignKeys(List<ColumnMetadata> columnMetadata,
                                                  Map<String, List<Object>> referencedData,
                                                  Map<String, List<Object>> allGeneratedData) {
        log.info("Generating database 1-1 composite foreign keys");
        if (columnMetadata.isEmpty()) return Collections.emptyList();

        int recordCount = columnMetadata.getFirst().getRecordCount();

        String commonSchema = null;
        String commonTable = null;

        var firstColFks = columnMetadata.getFirst().getForeignKeyMetadata();

        for (var fk : firstColFks) {
            String schema = fk.getReferencedSchema();
            String table = fk.getReferencedTable();

            boolean allMatch = true;
            for (int i = 1; i < columnMetadata.size(); i++) {
                boolean match = columnMetadata.get(i).getForeignKeyMetadata().stream()
                        .anyMatch(f -> f.getReferencedSchema().equals(schema) && f.getReferencedTable().equals(table));
                if (!match) {
                    allMatch = false;
                    break;
                }
            }

            if (allMatch) {
                commonSchema = schema;
                commonTable = table;
                break;
            }
        }

        final String targetSchema = commonSchema;
        final String targetTable = commonTable;

        // 1. Resolve source data lists for each column in the composite key
        List<List<Object>> sourceDataLists = columnMetadata.stream()
                .map(cm -> {
                    var fk = cm.getForeignKeyMetadata().stream()
                            .filter(f -> targetSchema == null || (f.getReferencedSchema().equals(targetSchema) && f.getReferencedTable().equals(targetTable)))
                            .findFirst()
                            .orElse(cm.getForeignKeyMetadata().getFirst());

                    String key = fk.getReferencedSchema() + "." + fk.getReferencedTable() + "." + fk.getReferencedColumn();
                    List<Object> data = allGeneratedData.get(key);
                    if (data == null) {
                        throw new IllegalStateException("Referenced data missing for " + key);
                    }
                    return data;
                })
                .toList();

        int available = sourceDataLists.getFirst().size();
        if (available < recordCount) {
            throw new RuntimeException("Not enough unique referenced values for 1-1 relationship. Needed: " + recordCount + ", available: " + available);
        }

        // 2. Generate random indices
        List<Integer> indices = java.util.stream.IntStream.range(0, available)
                .boxed()
                .collect(Collectors.collectingAndThen(Collectors.toList(), list -> {
                    Collections.shuffle(list, random);
                    return list.subList(0, recordCount);
                }));

        // 3. Map indices to values for each column
        return sourceDataLists.stream()
                .map(sourceList -> indices.stream()
                        .map(sourceList::get)
                        .toList())
                .collect(Collectors.toList());
    }
}