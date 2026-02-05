package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import java.util.List;

public class BooleanValueGenerator implements ValueGenerator {
    @Override
    public Object generateValue() {
        return Math.random() < 0.5 ? 'f' : 't';
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        throw new UnsupportedOperationException("BooleanValueGenerator does not support range-based generation.");
    }

    @Override
    public java.util.List<Object> generateValues(int count) {
        return List.of('f', 't');
    }

    @Override
    public java.util.List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        throw new UnsupportedOperationException("BooleanValueGenerator does not support range-based generation.");
    }
}
