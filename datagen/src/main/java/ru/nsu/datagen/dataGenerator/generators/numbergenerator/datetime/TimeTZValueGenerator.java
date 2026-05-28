package ru.nsu.datagen.dataGenerator.generators.numbergenerator.datetime;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

import java.time.LocalTime;
import java.time.OffsetTime;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class TimeTZValueGenerator extends ValueGeneratorAC {
    private final TimeValueGenerator generator = new TimeValueGenerator();

    public TimeTZValueGenerator() {
        super(OffsetTime.MIN, OffsetTime.MAX);
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
    public void generateValues(List<Object> values, int count, Object leftBorder, Object rightBorder) {
        OffsetTime left = convertToOffsetTime(leftBorder, OffsetTime.MIN);
        OffsetTime right = convertToOffsetTime(rightBorder, OffsetTime.MAX);

        if (!left.isBefore(right)) {
            log.warn("Left border {} >= right border {}, generating same time {} times", left, right, count);
            values.addAll(Collections.nCopies(count, left));
            return;
        }

        Set<Object> uniqueValues = new HashSet<>(values);
        long maxAttempts = count * 100L;
        long attempts = 0;
        int i = 0;

        while (i < count && attempts < maxAttempts) {
            Object val = generateValue(left, right);
            if (uniqueValues.add(val)) {
                values.add(val);
                i++;
            }
            attempts++;
        }

        if (values.size() < count) {
            log.error("Could not generate {} unique time with timezone values in range ({} to {}) after {} attempts. Generated only {} unique values.",
                    count, left, right, maxAttempts, values.size());
            throw new RuntimeException("Couldn`t generate all unique values with " + this.getClass()
                    + ". Done only " + values.size() + " out of " + count + " unique values.");
        }
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
