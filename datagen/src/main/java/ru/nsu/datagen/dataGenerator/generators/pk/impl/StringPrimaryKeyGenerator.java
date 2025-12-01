package ru.nsu.datagen.dataGenerator.generators.pk.impl;

import ru.nsu.datagen.dataGenerator.generators.pk.PrimaryKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.ArrayList;
import java.util.List;

public class StringPrimaryKeyGenerator implements PrimaryKeyGenerator {
    private final String alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuwxyz0123456789";

    public List<Object> generatePrimaryKeys(ColumnMetadata columnMetadata) {
        List<Object> keys = new ArrayList<>();
        int recordCount = columnMetadata.getRecordCount();
        Integer maxLength = columnMetadata.getMaxLength();

        for (int i = 0; i <= recordCount; i++) {
            String prefix = i / alphabet.length() > 0 ? (String) keys.get(i / alphabet.length() - 1) : "";
            String key = prefix + alphabet.charAt(i % alphabet.length()); //"PK_" + i;
            if (maxLength != null && key.length() > maxLength) {
                key = key.substring(0, maxLength);
            }
            keys.add(key);
        }

        return keys;
    }
}