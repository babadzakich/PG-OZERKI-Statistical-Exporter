package ru.nsu.datagen.dataGenerator.generators.numbergenerator.numeric;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class DoubleValueGenerator extends ValueGeneratorAC {
    private final Faker faker = new Faker();

    public DoubleValueGenerator() {
        super(-Double.MAX_VALUE, Double.MAX_VALUE);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        double left, right;
        try {
            left = (double) leftBorder;
            right = (double) rightBorder;
        } catch (ClassCastException e) {
            log.warn("Invalid border types for {} value generation: {} and {}, using MAX and -MAX values",
                    this.getClass().toString(), leftBorder.getClass(), rightBorder.getClass());
            left = -Double.MAX_VALUE;
            right = Double.MAX_VALUE;
        }
        return faker.number().randomDouble(15, (long) left, (long) right);
    }

    @Override
    public void generateValues(List<Object> values, int count, Object leftBorder, Object rightBorder) {
        double left, right;
        try {
            left = (double) leftBorder;
            right = (double) rightBorder;
        } catch (ClassCastException e) {
            log.warn("Invalid border types for {} list generation: {} and {}, using MAX and -MAX values",
                    this.getClass().toString(), leftBorder.getClass(), rightBorder.getClass());
            left = -Double.MAX_VALUE;
            right = Double.MAX_VALUE;
        }

        Set<Object> uniqueValues = new HashSet<>(values);
        long maxAttempts = count * 100L;
        long attempts = 0;
        int i = 0;

        for (; i < count && attempts < maxAttempts; attempts++) {
            Object val = faker.number().randomDouble(15, (long) left, (long) right);
            if (uniqueValues.add(val)) {
                values.add(val);
                i++;
            }
        }

        if (values.size() < count) {
            log.error("Could not generate {} unique double values in range ({} to {}) after {} attempts. Generated only {} unique values.",
                    count, left, right, maxAttempts, values.size());
            throw new RuntimeException("Couldn`t generate all unique values with " + this.getClass()
                    + ". Done only " + values.size() + " out of " + count + " unique values.");
        }
    }
}
