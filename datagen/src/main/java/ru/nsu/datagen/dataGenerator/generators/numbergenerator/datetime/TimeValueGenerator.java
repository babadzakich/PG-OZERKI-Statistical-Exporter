package ru.nsu.datagen.dataGenerator.generators.numbergenerator.datetime;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;

import java.time.LocalTime;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class TimeValueGenerator implements ValueGenerator {
    private final Faker faker = new Faker();
    @Override
    public Object generateValue() {
        return generateValue(LocalTime.MIN, LocalTime.MAX);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        LocalTime left, right;
        if (leftBorder instanceof LocalTime) {
            left = (LocalTime) leftBorder;
        } else {
            left = LocalTime.MIN;
        }

        if (rightBorder instanceof LocalTime) {
            right = (LocalTime) rightBorder;
        } else {
            right = LocalTime.MAX;
        }

        if (!left.isBefore(right)) {
            log.warn("Left time border is later than right time border, using default values");
            left = LocalTime.MIN;
            right = LocalTime.MAX;
        }

        return LocalTime.ofSecondOfDay(faker.time().between(left, right));
    }

    @Override
    public List<Object> generateValues(int count) {
        return generateValues(count, LocalTime.MIN, LocalTime.MAX);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        LocalTime left, right;
        if (leftBorder instanceof LocalTime) {
            left = (LocalTime) leftBorder;
        } else {
            left = LocalTime.MIN;
        }

        if (rightBorder instanceof LocalTime) {
            right = (LocalTime) rightBorder;
        } else {
            right = LocalTime.MAX;
        }

        if (!left.isBefore(right)) {
            log.warn("Left time border is later than right time border, using default values");
            left = LocalTime.MIN;
            right = LocalTime.MAX;
        }

        Set<LocalTime> uniqueValues = HashSet.newHashSet(count);
        long maxAttempts = 100L * count;
        long attempts = 0;

        while (uniqueValues.size() < count && attempts < maxAttempts) {
            uniqueValues.add(LocalTime.ofSecondOfDay(faker.time().between(left, right)));
            attempts++;
        }

        if (uniqueValues.size() < count) {
            log.warn("Couldn`t generate {} unique time values, having only {}", count, uniqueValues.size());
        }
        return new ArrayList<>(uniqueValues);
    }
}
