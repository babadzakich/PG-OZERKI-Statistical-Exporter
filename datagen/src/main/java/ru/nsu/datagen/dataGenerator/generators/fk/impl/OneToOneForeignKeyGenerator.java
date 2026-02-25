package ru.nsu.datagen.dataGenerator.generators.fk.impl;

import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.*;

public class OneToOneForeignKeyGenerator implements ForeignKeyGenerator {
    private final Random random = new Random();

    @Override
    public List<Object> generateForeignKeys(ColumnMetadata columnMetadata,
                                            Map<String, List<Object>> referencedData,
                                            Map<String, List<Object>> allGeneratedData) {

        String refTable = columnMetadata.getForeignKeyMetadata().getFirst().getReferencedTable();
        String refColumn = columnMetadata.getForeignKeyMetadata().getFirst().getReferencedColumn();
        String refKey = refTable + "." + refColumn;

        if (!referencedData.containsKey(refKey)) {
            throw new IllegalStateException("Referenced data not found: " + refKey);
        }

        List<Object> refValues = referencedData.get(refKey);
        List<Object> foreignKeys = new ArrayList<>();
        int recordCount = columnMetadata.getRecordCount();
        double nullPercentage = columnMetadata.getNullPercentage();

        // Для ONE_TO_ONE создаем прямое соответствие записей
        for (int i = 0; i < recordCount; i++) {
            if (random.nextDouble() < nullPercentage) {
                foreignKeys.add(null);
            } else if (i < refValues.size()) {
                // Прямое соответствие по индексу
                foreignKeys.add(refValues.get(i));
            } else {
                // Если записей больше чем в родительской таблице, берем случайные значения
                foreignKeys.add(refValues.get(random.nextInt(refValues.size())));
            }
        }

        return foreignKeys;
    }
}