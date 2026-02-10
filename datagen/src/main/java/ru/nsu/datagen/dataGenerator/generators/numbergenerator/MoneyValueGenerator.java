package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import ru.nsu.datagen.dataGenerator.generators.numbergenerator.numeric.NumericValueGenerator;

import java.math.BigDecimal;
import java.math.RoundingMode;
import java.util.List;

public class MoneyValueGenerator implements ValueGenerator {
    private final NumericValueGenerator numericValueGenerator = new NumericValueGenerator(17, 2);
    private final BigDecimal maxValue = new BigDecimal("92233720368547758.07");
    private final BigDecimal minValue = new BigDecimal("-92233720368547758.08");
    @Override
    public Object generateValue() {
        return numericValueGenerator.generateValue(minValue, maxValue);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        BigDecimal left, right;
        if (leftBorder instanceof Number && rightBorder instanceof Number) {
            left = new BigDecimal(leftBorder.toString()).setScale(2, RoundingMode.HALF_UP);
            right = new BigDecimal(rightBorder.toString()).setScale(2, RoundingMode.HALF_UP);
        } else {
            left = minValue;
            right = maxValue;
        }

        return numericValueGenerator.generateValue(left, right);
    }

    @Override
    public List<Object> generateValues(int count) {
        return numericValueGenerator.generateValues(count, minValue, maxValue);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        BigDecimal left, right;
        if (leftBorder instanceof Number && rightBorder instanceof Number) {
            left = new BigDecimal(leftBorder.toString()).setScale(2, RoundingMode.HALF_UP);
            right = new BigDecimal(rightBorder.toString()).setScale(2, RoundingMode.HALF_UP);
        } else {
            left = minValue;
            right = maxValue;
        }
        return numericValueGenerator.generateValues(count, left, right);
    }
}
