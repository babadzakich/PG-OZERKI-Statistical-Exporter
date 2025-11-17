package ru.nsu.datagen.dataGenerator.generators.pk.impl;

import ru.nsu.datagen.dataGenerator.generators.pk.PrimaryKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.ArrayList;
import java.util.List;

public class StringPrimaryKeyGenerator implements PrimaryKeyGenerator {
    //private String alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuwxyz0123456789"
    public List<Object> generatePrimaryKeys(ColumnMetadata columnMetadata) {
        List<Object> keys = new ArrayList<>();
        int recordCount = columnMetadata.getRecordCount();
        Integer maxLength = columnMetadata.getMaxLength();
        // boolean hasFixedLength = columnMetadata.
        for (int i = 1; i <= recordCount; i++) {
            String key = String.valueOf(i); //"PK_" + i;
            if (maxLength != null && key.length() > maxLength) {
                key = key.substring(0, maxLength);
            }
            keys.add(key);
        }

        return keys;
    }
}