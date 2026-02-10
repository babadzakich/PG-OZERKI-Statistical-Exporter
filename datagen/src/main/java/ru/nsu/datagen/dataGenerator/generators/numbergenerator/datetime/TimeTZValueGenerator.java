package ru.nsu.datagen.dataGenerator.generators.numbergenerator.datetime;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;

import java.time.LocalTime;
import java.time.OffsetTime;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class TimeTZValueGenerator implements ValueGenerator {
    private final TimeValueGenerator generator = new TimeValueGenerator();

    @Override
    public Object generateValue() {
        return generateValue(OffsetTime.MIN, OffsetTime.MAX);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        OffsetTime left = convertToOffsetTime(leftBorder, OffsetTime.MIN);
        OffsetTime right = convertToOffsetTime(rightBorder, OffsetTime.MAX);

        if (!left.isBefore(right)) {
            log.warn("Left border {} >= right border {}, using left value", left, right);
            return left;
        }

        LocalTime generatedTime = (LocalTime) generator.generateValue(left.toLocalTime(), right.toLocalTime());
        return OffsetTime.of(generatedTime, left.getOffset());
    }

    @Override
    public List<Object> generateValues(int count) {
        return generateValues(count, OffsetTime.MIN, OffsetTime.MAX);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        OffsetTime left = convertToOffsetTime(leftBorder, OffsetTime.MIN);
        OffsetTime right = convertToOffsetTime(rightBorder, OffsetTime.MAX);

        if (!left.isBefore(right)) {
            log.warn("Left border {} >= right border {}, generating same time {} times", left, right, count);
            return new ArrayList<>(Collections.nCopies(count, left));
        }

        Set<OffsetTime> uniqueValues = new HashSet<>();
        long maxAttempts = count * 100L;
        long attempts = 0;

        while (uniqueValues.size() < count && attempts < maxAttempts) {
            LocalTime generatedTime = (LocalTime) generator.generateValue(left.toLocalTime(), right.toLocalTime());
            uniqueValues.add(OffsetTime.of(generatedTime, left.getOffset()));
            attempts++;
        }

        if (uniqueValues.size() < count) {
            log.warn("Could not generate {} unique time with timezone values in range ({} to {}) after {} attempts. Generated only {} unique values.",
                    count, left, right, maxAttempts, uniqueValues.size());
        }

        return new ArrayList<>(uniqueValues);
    }

    private OffsetTime convertToOffsetTime(Object value, OffsetTime defaultValue) {
        if (value instanceof OffsetTime offsetTime) {
            return offsetTime;
        }
        if (value != null) {
            try {
                return OffsetTime.parse(value.toString());
            } catch (Exception e) {
                log.debug("Could not parse OffsetTime from {}, using default", value);
            }
        }
        return defaultValue;
    }
}
