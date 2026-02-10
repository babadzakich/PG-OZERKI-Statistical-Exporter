package ru.nsu.datagen.dataGenerator.generators.numbergenerator.numeric;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class FloatValueGenerator implements ValueGenerator {
    private final Faker faker = new Faker();

    @Override
    public Object generateValue() {
        return generateValue(-Float.MAX_VALUE, Float.MAX_VALUE);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        float left, right;
        try {
            left = (float) leftBorder;
            right = (float) rightBorder;
        } catch (ClassCastException e) {
            log.warn("Invalid border types for {} value generation: {} and {}, using MAX and -MAX values",
                    this.getClass().toString(), leftBorder.getClass(), rightBorder.getClass());
            left = -Float.MAX_VALUE;
            right = Float.MAX_VALUE;
        }

        double value = faker.number().randomDouble(6, (long)left, (long)right);
        return (float) value;
    }

    @Override
    public List<Object> generateValues(int count) {
        return generateValues(count, -Float.MAX_VALUE, Float.MAX_VALUE);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        float left, right;
        try {
            left = (float) leftBorder;
            right = (float) rightBorder;
        } catch (ClassCastException e) {
            log.warn("Invalid border types for {} list generation: {} and {}, using MAX and -MAX values",
                    this.getClass().toString(), leftBorder.getClass(), rightBorder.getClass());
            left = -Float.MAX_VALUE;
            right = Float.MAX_VALUE;
        }

        Set<Float> uniqueValues = new HashSet<>();
        long maxAttempts = count * 100L;
        long attempts = 0, i = 0;

        while (i < count && attempts < maxAttempts) {
            double value = faker.number().randomDouble(6, (long)left, (long)right);
            i += uniqueValues.add((float) value) ? 1 : 0;
            attempts++;
        }

        if (uniqueValues.size() < count) {
            log.warn("Could not generate {} unique float values in range ({} to {}) after {} attempts. Generated only {} unique values.",
                    count, left, right, maxAttempts, uniqueValues.size());
        }

        return new ArrayList<>(uniqueValues);
    }
}
