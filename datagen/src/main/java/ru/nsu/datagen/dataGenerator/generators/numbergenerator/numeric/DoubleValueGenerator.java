package ru.nsu.datagen.dataGenerator.generators.numbergenerator.numeric;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class DoubleValueGenerator implements ValueGenerator {
    private final Faker faker = new Faker();

    @Override
    public Object generateValue() {
        return generateValue(-Double.MAX_VALUE, Double.MAX_VALUE);
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
        return faker.number().randomDouble(15, (long)left, (long)right);
    }

    @Override
    public List<Object> generateValues(int count) {
        return generateValues(count, -Double.MAX_VALUE, Double.MAX_VALUE);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
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

        Set<Double> uniqueValues = new HashSet<>();
        long maxAttempts = count * 100L;
        long attempts = 0;
        for (int i = 0; i < count && attempts < maxAttempts;) {
            double value = faker.number().randomDouble(15, (long)left, (long)right);
            i += uniqueValues.add(value) ? 1 : 0;
            attempts++;
        }
        return new ArrayList<>(uniqueValues);
    }
}
