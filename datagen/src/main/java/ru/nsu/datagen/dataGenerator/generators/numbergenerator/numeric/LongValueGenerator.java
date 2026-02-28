package ru.nsu.datagen.dataGenerator.generators.numbergenerator.numeric;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

import java.math.BigInteger;
import java.util.Collections;
import java.util.List;

@Slf4j
public class LongValueGenerator extends ValueGeneratorAC {
    private final Faker faker = new Faker();

    public LongValueGenerator() {
        super(Long.MIN_VALUE, Long.MAX_VALUE);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        long left, right;
        try {
            left = (long) leftBorder;
            right = (long) rightBorder;
        } catch (ClassCastException e) {
            log.warn("Invalid border types for {} value generation: {} and {}, using MAX and MIN values", this.getClass().toString(), leftBorder.getClass(), rightBorder.getClass());
            left = Long.MIN_VALUE;
            right = Long.MAX_VALUE;
        }
        return faker.number().numberBetween(left, right);
    }

    @Override
    public void generateValues(List<Object> values, int count, Object leftBorder, Object rightBorder) {
        long left, right;
        try {
            left = (long) leftBorder;
            right = (long) rightBorder;
        } catch (ClassCastException e) {
            log.warn("Invalid border types for {} list generation: {} and {}, using MAX and MIN values", this.getClass().toString(), leftBorder.getClass(), rightBorder.getClass());
            left = Long.MIN_VALUE;
            right = Long.MAX_VALUE;
        }

        BigInteger range = BigInteger.valueOf(right).subtract(BigInteger.valueOf(left));
        if (range.compareTo(BigInteger.valueOf(count - 1)) >= 0) {
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
    }
}
