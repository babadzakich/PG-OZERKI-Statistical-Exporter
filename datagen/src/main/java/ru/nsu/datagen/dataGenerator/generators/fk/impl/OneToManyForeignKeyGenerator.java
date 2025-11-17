package ru.nsu.datagen.dataGenerator.generators.fk.impl;

import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.*;

public class OneToManyForeignKeyGenerator implements ForeignKeyGenerator {
    private final Random random = new Random();
    
    public List<Object> generateForeignKeys(ColumnMetadata columnMetadata,
                                            Map<String, List<Object>> referencedData,
                                            Map<String, Map<String, List<Object>>> allGeneratedData) {

        String refTable = columnMetadata.getForeignKeyMetadata().getReferencedTable();
        String refColumn = columnMetadata.getForeignKeyMetadata().getReferencedColumn();
        String refKey = refTable + "." + refColumn;

        System.out.println("tryna find refKey: " + refKey);
        if (!referencedData.containsKey(refKey)) {
            throw new IllegalStateException("Referenced data not found: " + refKey);
        }

        List<Object> refValues = referencedData.get(refKey);
        List<Object> foreignKeys = new ArrayList<>();
        int recordCount = columnMetadata.getRecordCount();
        double nullPercentage = columnMetadata.getNullPercentage();

        // Для ONE_TO_MANY выбираем случайные родительские значения с предпочтением некоторых
        Map<Object, Integer> usageCount = new HashMap<>();

        for (int i = 0; i < recordCount; i++) {
            if (random.nextDouble() < nullPercentage) {
                foreignKeys.add(null);
            } else if (!refValues.isEmpty()) {
                Object selectedValue = selectValueWithDistribution(refValues, usageCount);
                foreignKeys.add(selectedValue);
                usageCount.merge(selectedValue, 1, Integer::sum);
            } else {
                foreignKeys.add(null);
            }
        }

        return foreignKeys;
    }

    private Object selectValueWithDistribution(List<Object> refValues, Map<Object, Integer> usageCount) {
        // Предпочтение отдаем значениям, которые еще не использовались или использовались мало
        List<Object> candidates = new ArrayList<>();

        for (Object value : refValues) {
            int used = usageCount.getOrDefault(value, 0);
            // Чем меньше использовалось значение, тем больше шансов его выбрать
            int weight = Math.max(1, 10 - used);
            for (int i = 0; i < weight; i++) {
                candidates.add(value);
            }
        }

        return candidates.get(new Random().nextInt(candidates.size()));
    }
}