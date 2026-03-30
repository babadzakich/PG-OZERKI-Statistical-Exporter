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
                                                  Map<String, List<Object>> allGeneratedData) {
        log.info("Generating database 1-1 composite foreign keys");
        if (columnMetadata.isEmpty()) return Collections.emptyList();

        int recordCount = columnMetadata.getFirst().getRecordCount();
        int parentTableCount = columnMetadata.getFirst().getForeignKeyMetadata().size();

        // Строим первый сет кортежей
        var firstFk = columnMetadata.getFirst().getForeignKeyMetadata().getFirst();
        int parentSize = allGeneratedData.get(
                firstFk.getReferencedSchema() + "." + firstFk.getReferencedTable() + "." + firstFk.getReferencedColumn()
        ).size();

        Set<List<Object>> intersection = new LinkedHashSet<>();
        for (int i = 0; i < parentSize; i++) {
            List<Object> tuple = new ArrayList<>(columnMetadata.size());
            for (ColumnMetadata col : columnMetadata) {
                var fk = col.getForeignKeyMetadata().getFirst();
                String refKey = fk.getReferencedSchema() + "." + fk.getReferencedTable() + "." + fk.getReferencedColumn();
                tuple.add(allGeneratedData.get(refKey).get(i));
            }
            intersection.add(tuple);
        }

        // Пересекаем с каждым следующим родителем
        for (int tableIdx = 1; tableIdx < parentTableCount; tableIdx++) {
            var fk0 = columnMetadata.getFirst().getForeignKeyMetadata().get(tableIdx);
            int pSize = allGeneratedData.get(
                    fk0.getReferencedSchema() + "." + fk0.getReferencedTable() + "." + fk0.getReferencedColumn()
            ).size();

            Set<List<Object>> currentKeys = new HashSet<>();
            for (int i = 0; i < pSize; i++) {
                List<Object> tuple = new ArrayList<>(columnMetadata.size());
                for (ColumnMetadata col : columnMetadata) {
                    var fk = col.getForeignKeyMetadata().get(tableIdx);
                    String refKey = fk.getReferencedSchema() + "." + fk.getReferencedTable() + "." + fk.getReferencedColumn();
                    tuple.add(allGeneratedData.get(refKey).get(i));
                }
                currentKeys.add(tuple);
            }
            intersection.retainAll(currentKeys);
        }

        if (intersection.size() < recordCount) {
            throw new RuntimeException("Not enough unique referenced tuples for 1-1 relationship. Needed: "
                    + recordCount + ", available: " + intersection.size());
        }

        // Shuffled subList — без повторений (1-1)
        List<List<Object>> validTuples = new ArrayList<>(intersection);
        Collections.shuffle(validTuples, random);
        validTuples = validTuples.subList(0, recordCount);

        // Транспонируем: из списка кортежей в список колонок
        List<List<Object>> result = new ArrayList<>(columnMetadata.size());
        for (int j = 0; j < columnMetadata.size(); j++) {
            result.add(new ArrayList<>(recordCount));
        }
        for (List<Object> tuple : validTuples) {
            for (int j = 0; j < columnMetadata.size(); j++) {
                result.get(j).add(tuple.get(j));
            }
        }

        return result;
    }
}