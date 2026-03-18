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
    public List<List<Object>> generateForeignKeys(List<ColumnMetadata> columnMetadata, Map<String, List<Object>> referencedData, Map<String, List<Object>> allGeneratedData) {
        return List.of();
    }
}