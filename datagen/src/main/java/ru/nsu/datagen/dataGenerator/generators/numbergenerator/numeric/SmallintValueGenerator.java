package ru.nsu.datagen.dataGenerator.generators.numbergenerator.numeric;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

@Slf4j
public class SmallintValueGenerator implements ValueGenerator {
    private final Faker faker = new Faker();

    @Override
    public Object generateValue() {
        return generateValue(Short.MIN_VALUE, Short.MAX_VALUE);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        short left, right;
        try {
            left = (short)leftBorder;
            right = (short)rightBorder;
        } catch (ClassCastException e) {
            log.warn("Invalid border types for {} value generation: {} and {}, using MAX and MIN values", this.getClass().toString(), leftBorder.getClass(), rightBorder.getClass());
            left = Short.MIN_VALUE;
            right = Short.MAX_VALUE;
        }

        return (short) faker.number().numberBetween((long)left, (long)right);
    }

    @Override
    public List<Object> generateValues(int count) {
        return generateValues(count, Short.MIN_VALUE, Short.MAX_VALUE);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        short left, right;
        try {
            left = (short) leftBorder;
            right = (short) rightBorder;
        } catch (ClassCastException e) {
            log.warn("Invalid border types for {} list generation: {} and {}, using MAX and MIN values", this.getClass().toString(), leftBorder.getClass(), rightBorder.getClass());
            left = Short.MIN_VALUE;
            right = Short.MAX_VALUE;
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
