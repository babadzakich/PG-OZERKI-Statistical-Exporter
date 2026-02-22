package ru.nsu.datagen.dataGenerator.generators.numbergenerator.numeric;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

import java.math.BigDecimal;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class NumericValueGenerator extends ValueGeneratorAC {
    private final int scale;
    private final double maxNumericValue;
    private final Faker faker = new Faker();

    public NumericValueGenerator(int precision, int scale) {
        super(0, Math.pow(10, precision - scale) - Math.pow(10, -scale));
        this.scale = scale;
        this.maxNumericValue = Math.pow(10, precision - scale) - Math.pow(10, -scale);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        double left, right;
        if (leftBorder instanceof Number && rightBorder instanceof Number) {
            left = ((Number) leftBorder).doubleValue();
            right = ((Number) rightBorder).doubleValue();
        } else {
            left = 0;
            right = maxNumericValue;
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
    public void generateValues(List<Object> values, int count, Object leftBorder, Object rightBorder) {
        Set<Object> uniqueValues = new HashSet<>(values);
        long maxAttempts = count * 100L;
        long attempts = 0;
        int i = 0;

        while (i < count && attempts < maxAttempts) {
            Object val = generateValue(leftBorder, rightBorder);
            if (uniqueValues.add(val)) {
                values.add(val);
                i++;
            }
            attempts++;
        }

        if (values.size() < count) {
            log.error("Could only generate {} unique values out of requested {}, consider increasing the range or reducing the count",
                    values.size(), count);
            throw new RuntimeException("Couldn`t generate all unique values with " + this.getClass()
                    + ". Done only " + values.size() + " out of " + count + " unique values.");
        }
    }
}
