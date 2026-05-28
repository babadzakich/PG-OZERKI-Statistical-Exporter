package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;

import java.util.List;

@Slf4j
public class JSONValueGenerator extends ValueGeneratorAC {
    private final Faker faker = new Faker();

    @Override
    public Object generateValue() {
        return faker.json();
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        log.error("JSONValueGenerator does not support range-based generation.");
        throw new UnsupportedOperationException("JSONValueGenerator does not support range-based generation.");
    }

    @Override
    public void generateValues(List<Object> values, int count, Object leftBorder, Object rightBorder) {
        log.error("JSONValueGenerator does not support range-based list generation.");
        throw new UnsupportedOperationException("JSONValueGenerator does not support range-based list generation.");
    }
}
