package ru.nsu.datagen.dataGenerator.generators.numbergenerator.binary;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class ByteaValueGenerator extends ValueGeneratorAC {
    private final int length;
    private final Faker faker = new Faker();

    public ByteaValueGenerator(int length) {
        this.length = length;
    }

    @Override
    public Object generateValue() {
        return "\\x" + faker.random().hex(length * 2);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        throw new UnsupportedOperationException("ByteaValueGenerator does not support range-based generation.");
    }

    @Override
    public void generateValues(List<Object> values, int count, Object leftBorder, Object rightBorder) {
        Set<Object> unique = new HashSet<>(values);
        long maxAttempts = count * 100L;
        long attempts = 0;
        int i = 0;
        while (i < count && attempts < maxAttempts) {
            Object val = generateValue();
            if (unique.add(val)) {
                values.add(val);
                i++;
            }
            attempts++;
        }
        if (values.size() < count) {
            log.error("Could not generate {} unique bytea values after {} attempts. Generated only {}.",
                    count, maxAttempts, values.size());
            throw new RuntimeException("Couldn't generate all unique bytea values. Done only "
                    + values.size() + " out of " + count + " unique values.");
        }
    }
}
