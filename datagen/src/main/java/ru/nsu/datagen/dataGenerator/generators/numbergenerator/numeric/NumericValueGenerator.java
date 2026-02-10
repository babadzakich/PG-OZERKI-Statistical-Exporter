package ru.nsu.datagen.dataGenerator.generators.numbergenerator.numeric;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;

import java.math.BigDecimal;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class NumericValueGenerator implements ValueGenerator {
    private final int scale;
    private final double maxValue;
    private final Faker faker = new Faker();
    public NumericValueGenerator(int precision, int scale) {
        this.scale = scale;
            this.maxValue = Math.pow(10, precision - scale) - Math.pow(10, -scale);
    }
    @Override
    public Object generateValue() {
        return generateValue(0, maxValue);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        double left, right;
        if (leftBorder instanceof Number && rightBorder instanceof Number) {
            left = ((Number) leftBorder).doubleValue();
            right = ((Number) rightBorder).doubleValue();
        } else {
            left = 0;
            right = maxValue;
        }

        if (left > right) {
            double temp = left;
            left = right;
            right = temp;
        }

        double val = left + (faker.random().nextDouble() * (right - left));
        return BigDecimal.valueOf(val).setScale(scale, java.math.RoundingMode.HALF_UP);
    }

    @Override
    public List<Object> generateValues(int count) {
        return generateValues(count, 0, maxValue);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        Set<Object> uniqueValues = new HashSet<>();
        long maxAttempts = count * 100L;
        long attempts = 0;
        while (uniqueValues.size() < count && attempts < maxAttempts) {
            uniqueValues.add(generateValue(leftBorder, rightBorder));
            attempts++;
        }
        if (uniqueValues.size() < count) {
            log.warn("Could only generate {} unique values out of requested {}, consider increasing the range or reducing the count",
                    uniqueValues.size(), count);
        }
        return new ArrayList<>(uniqueValues);
    }
}
