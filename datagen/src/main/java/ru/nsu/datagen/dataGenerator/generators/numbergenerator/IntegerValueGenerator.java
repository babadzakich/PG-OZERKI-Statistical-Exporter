package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

@Slf4j
public class IntegerValueGenerator implements ValueGenerator {
    private final Faker faker = new Faker();

    @Override
    public Object generateValue() {
        return generateValue(Integer.MIN_VALUE, Integer.MAX_VALUE);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        int left, right;
        try {
            left = (int)leftBorder;
            right = (int)rightBorder;
        } catch (ClassCastException e) {
            log.warn("Invalid border types for {} value generation: {} and {}, using MAX and MIN values", this.getClass().toString(), leftBorder.getClass(), rightBorder.getClass());
            left = Integer.MIN_VALUE;
            right = Integer.MAX_VALUE;
        }
        // Используем long для генерации, но приводим к int
        return (int) faker.number().numberBetween((long)left, (long)right);
    }

    @Override
    public List<Object> generateValues(int count) {
        return generateValues(count, Integer.MIN_VALUE, Integer.MAX_VALUE);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        int left, right;
        try {
            left = (int) leftBorder;
            right = (int) rightBorder;
        } catch (ClassCastException e) {
            log.warn("Invalid border types for {} list generation: {} and {}, using MAX and MIN values", this.getClass().toString(), leftBorder.getClass(), rightBorder.getClass());
            left = Integer.MIN_VALUE;
            right = Integer.MAX_VALUE;
        }

        List<Object> values = new ArrayList<>();
        long range = (long)right - (long)left;
        if (range >= count - 1) {
            for (int i = 0; i < count; i++) {
                values.add(left + i);
            }
            Collections.shuffle(values);
        } else {
            log.warn("Requested count {} exceeds the number of unique values in the given range ({} to {}), adding more than right border", count, left, right);
            for (int i = 0; i < count; i++) {
                values.add(left + i);
            }
        }
        return values;
    }
}
