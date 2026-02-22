package ru.nsu.datagen.dataGenerator.generators.numbergenerator.numeric;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class FloatValueGenerator extends ValueGeneratorAC {
    private final Faker faker = new Faker();

    public FloatValueGenerator() {
        super(-Float.MAX_VALUE, Float.MAX_VALUE);
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
        return (float) faker.number().randomDouble(6, (long) left, (long) right);
    }

    @Override
    public void generateValues(List<Object> values, int count, Object leftBorder, Object rightBorder) {
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

        Set<Object> uniqueValues = new HashSet<>(values);
        long maxAttempts = count * 100L;
        long attempts = 0;
        int i = 0;

        while (i < count && attempts < maxAttempts) {
            Object val = generateValue(left, right);
            if (uniqueValues.add(val)) {
                values.add(val);
                i++;
            }
            attempts++;
        }

        if (values.size() < count) {
            log.error("Could not generate {} unique float values in range ({} to {}) after {} attempts. Generated only {} unique values.",
                    count, left, right, maxAttempts, values.size());
            throw new RuntimeException("Couldn`t generate all unique values with " + this.getClass()
                    + ". Done only " + values.size() + " out of " + count + " unique values.");
        }
    }
}
