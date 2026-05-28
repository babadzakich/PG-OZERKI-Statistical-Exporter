package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import ru.nsu.datagen.dataGenerator.generators.numbergenerator.numeric.NumericValueGenerator;

import java.math.BigDecimal;
import java.math.RoundingMode;

public class MoneyValueGenerator extends ValueGeneratorAC {
    private static final BigDecimal MIN_MONEY = new BigDecimal("-92233720368547758.08");
    private static final BigDecimal MAX_MONEY = new BigDecimal("92233720368547758.07");

    private final NumericValueGenerator numericValueGenerator = new NumericValueGenerator(17, 2);

    public MoneyValueGenerator() {
        super(MIN_MONEY, MAX_MONEY);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        BigDecimal left, right;
        if (leftBorder instanceof Number && rightBorder instanceof Number) {
            left = new BigDecimal(leftBorder.toString()).setScale(2, RoundingMode.HALF_UP);
            right = new BigDecimal(rightBorder.toString()).setScale(2, RoundingMode.HALF_UP);
        } else {
            left = MIN_MONEY;
            right = MAX_MONEY;
        }
        return numericValueGenerator.generateValue(left, right);
    }
}
