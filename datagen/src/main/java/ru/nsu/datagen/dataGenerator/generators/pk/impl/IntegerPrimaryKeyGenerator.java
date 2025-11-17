package ru.nsu.datagen.dataGenerator.generators.pk.impl;

import ru.nsu.datagen.dataGenerator.generators.pk.PrimaryKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.ArrayList;
import java.util.List;

public class IntegerPrimaryKeyGenerator implements PrimaryKeyGenerator {
    public List<Object> generatePrimaryKeys(ColumnMetadata columnMetadata) {
        List<Object> keys = new ArrayList<>();
        int recordCount = columnMetadata.getRecordCount();

        for (int i = 1; i <= recordCount; i++) {
            keys.add(i);
        }

        return keys;
    }
}