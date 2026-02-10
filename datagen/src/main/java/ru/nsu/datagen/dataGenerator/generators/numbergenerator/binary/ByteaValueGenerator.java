package ru.nsu.datagen.dataGenerator.generators.numbergenerator.binary;

import net.datafaker.Faker;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;

import java.util.Set;

public class ByteaValueGenerator implements ValueGenerator {
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
    public java.util.List<Object> generateValues(int count) {
        Set<String> uniqueValues = new java.util.HashSet<>();
        long maxAttempts = count * 100L;
        long attempts = 0;
        while (uniqueValues.size() < count && attempts < maxAttempts) {
            uniqueValues.add((String) generateValue());
            attempts++;
        }
        return new java.util.ArrayList<>(uniqueValues);
    }

    @Override
    public java.util.List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        throw new UnsupportedOperationException("ByteaValueGenerator does not support range-based list generation.");
    }
}
