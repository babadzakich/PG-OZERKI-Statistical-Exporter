package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import lombok.extern.slf4j.Slf4j;

import java.util.List;
@Slf4j
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
        if (count > 2) {
            log.error("Couldn`t generate more than 2 boolean values");
            throw new RuntimeException("Can`t generate more than 2 unique boolean values");
        }
        return List.of('f', 't');
    }

    @Override
    public java.util.List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        throw new UnsupportedOperationException("BooleanValueGenerator does not support range-based generation.");
    }
}
