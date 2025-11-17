package ru.nsu.datagen.dataGenerator.generators.fk.impl;

import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.*;

public class ManyToOneForeignKeyGenerator implements ForeignKeyGenerator {
    private final Random random = new Random();

    @Override
    public List<Object> generateForeignKeys(ColumnMetadata columnMetadata,
                                            Map<String, List<Object>> referencedData,
                                            Map<String, Map<String, List<Object>>> allGeneratedData) {

        String refTable = columnMetadata.getForeignKeyMetadata().getReferencedTable();
        String refColumn = columnMetadata.getForeignKeyMetadata().getReferencedColumn();
        String refKey = refTable + "." + refColumn;

        if (!referencedData.containsKey(refKey)) {
            throw new IllegalStateException("Referenced data not found: " + refKey);
        }

        List<Object> refValues = referencedData.get(refKey);
        List<Object> foreignKeys = new ArrayList<>();
        int recordCount = columnMetadata.getRecordCount();
        double nullPercentage = columnMetadata.getNullPercentage();

        // Для MANY_TO_ONE равномерно распределяем по родительским значениям
        for (int i = 0; i < recordCount; i++) {
            if (random.nextDouble() < nullPercentage) {
                foreignKeys.add(null);
            } else if (!refValues.isEmpty()) {
                foreignKeys.add(refValues.get(random.nextInt(refValues.size())));
            } else {
                foreignKeys.add(null);
            }
        }

        return foreignKeys;
    }
}