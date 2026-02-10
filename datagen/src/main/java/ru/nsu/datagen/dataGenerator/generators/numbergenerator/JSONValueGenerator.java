package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class JSONValueGenerator implements ValueGenerator {
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
    public List<Object> generateValues(int count) {
        Set<String> uniqueValues = new HashSet<>();
        long maxAttempts = count * 100L;
        long attempts = 0;
        while (uniqueValues.size() < count && attempts < maxAttempts) {
            uniqueValues.add(faker.json());
            attempts++;
        }
        return new ArrayList<>(uniqueValues);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        log.error("JSONValueGenerator does not support range-based list generation.");
        throw new UnsupportedOperationException("JSONValueGenerator does not support range-based list generation.");
    }
}
