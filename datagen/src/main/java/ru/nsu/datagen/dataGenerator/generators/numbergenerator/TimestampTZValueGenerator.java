package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;

import java.sql.Timestamp;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.*;

@Slf4j
public class TimestampTZValueGenerator implements ValueGenerator {
    private static final Instant PG_MIN_TIMESTAMP = LocalDateTime.of(-4712, 1, 1, 0, 0, 0).toInstant(ZoneOffset.UTC);
    private static final Instant PG_MAX_TIMESTAMP = LocalDateTime.of(294276, 12, 31, 23, 59, 59).toInstant(ZoneOffset.UTC);

    private final Faker faker = new Faker();

    @Override
    public Object generateValue() {
        return faker.timeAndDate().between(PG_MIN_TIMESTAMP, PG_MAX_TIMESTAMP);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        Instant left = convertToInstant(leftBorder, PG_MIN_TIMESTAMP);
        Instant right = convertToInstant(rightBorder, PG_MAX_TIMESTAMP);

        if (left.isAfter(right) || left.equals(right)) {
            log.warn("Left border {} >= right border {}, using left value", left, right);
            return left;
        }

        return faker.timeAndDate().between(left, right);
    }

    @Override
    public java.util.List<Object> generateValues(int count) {
        return generateValues(count, PG_MIN_TIMESTAMP, PG_MAX_TIMESTAMP);
    }

    @Override
    public java.util.List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        Instant left = convertToInstant(leftBorder, PG_MIN_TIMESTAMP);
        Instant right = convertToInstant(rightBorder, PG_MAX_TIMESTAMP);

        if (!left.isBefore(right)) {
            log.warn("Left border {} >= right border {}, generating same timestamp {} times", left, right, count);
            return new ArrayList<>(Collections.nCopies(count, left));
        }

        Set<Instant> uniqueValues = new HashSet<>();
        int maxAttempts = count * 100;
        int attempts = 0;

        while (uniqueValues.size() < count && attempts < maxAttempts) {
            uniqueValues.add(faker.timeAndDate().between(left, right));
            attempts++;
        }

        if (uniqueValues.size() < count) {
            log.warn("Could not generate {} unique timestamps in range ({} to {}) after {} attempts. Generated only {} unique values.",
                    count, left, right, maxAttempts, uniqueValues.size());
        }

        return new ArrayList<>(uniqueValues);
    }

    private Instant convertToInstant(Object value, Instant defaultValue) {
        if (value instanceof Instant instant) {
            return instant;
        }
        if (value instanceof Timestamp timestamp) {
            return timestamp.toInstant();
        }
        if (value != null) {
            try {
                return Instant.parse(value.toString());
            } catch (Exception e) {
                log.debug("Could not parse timestamp from {}, using default", value);
            }
        }
        return defaultValue;
    }
}
