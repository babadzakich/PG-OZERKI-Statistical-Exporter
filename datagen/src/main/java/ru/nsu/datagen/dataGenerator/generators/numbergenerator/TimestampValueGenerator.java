package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;

import java.sql.Timestamp;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class TimestampValueGenerator implements ValueGenerator {
    private final Faker faker = new Faker();

    private static final LocalDateTime PG_MIN_TIMESTAMP = LocalDateTime.of(-4712, 1, 1, 0, 0, 0);
    private static final LocalDateTime PG_MAX_TIMESTAMP = LocalDateTime.of(294276, 12, 31, 23, 59, 59);

    @Override
    public Object generateValue() {
        return faker.timeAndDate().between(
                PG_MIN_TIMESTAMP.toInstant(ZoneOffset.UTC),
                PG_MAX_TIMESTAMP.toInstant(ZoneOffset.UTC)
        ).atZone(ZoneOffset.UTC).toLocalDateTime();
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        LocalDateTime left = convertToLocalDateTime(leftBorder, PG_MIN_TIMESTAMP);
        LocalDateTime right = convertToLocalDateTime(rightBorder, PG_MAX_TIMESTAMP);

        if (left.isAfter(right) || left.equals(right)) {
            log.warn("Left border {} >= right border {}, using left value", left, right);
            return left;
        }

        return faker.timeAndDate().between(
                left.toInstant(ZoneOffset.UTC),
                right.toInstant(ZoneOffset.UTC)
        ).atZone(ZoneOffset.UTC).toLocalDateTime();
    }

    @Override
    public List<Object> generateValues(int count) {
        return generateValues(count, PG_MIN_TIMESTAMP, PG_MAX_TIMESTAMP);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        LocalDateTime left = convertToLocalDateTime(leftBorder, PG_MIN_TIMESTAMP);
        LocalDateTime right = convertToLocalDateTime(rightBorder, PG_MAX_TIMESTAMP);

        if (!left.isBefore(right)) {
            log.warn("Left border {} >= right border {}, generating same timestamp {} times", left, right, count);
            return new ArrayList<>(Collections.nCopies(count, left));
        }

        Set<LocalDateTime> uniqueValues = new HashSet<>();
        int maxAttempts = count * 100;
        int attempts = 0;

        while (uniqueValues.size() < count && attempts < maxAttempts) {
            uniqueValues.add(faker.timeAndDate().between(
                    left.toInstant(ZoneOffset.UTC),
                    right.toInstant(ZoneOffset.UTC)
            ).atZone(ZoneOffset.UTC).toLocalDateTime());
            attempts++;
        }

        if (uniqueValues.size() < count) {
            log.warn("Could not generate {} unique timestamps in range ({} to {}) after {} attempts. Generated only {} unique values.",
                    count, left, right, maxAttempts, uniqueValues.size());
        }

        return new ArrayList<>(uniqueValues);
    }

    private LocalDateTime convertToLocalDateTime(Object value, LocalDateTime defaultValue) {
        if (value instanceof LocalDateTime localDateTime) {
            return localDateTime;
        }
        if (value instanceof Instant instant) {
            return LocalDateTime.ofInstant(instant, ZoneOffset.UTC);
        }
        if (value instanceof Timestamp timestamp) {
            return timestamp.toLocalDateTime();
        }
        if (value != null) {
             try {
                 return Instant.parse(value.toString()).atZone(ZoneOffset.UTC).toLocalDateTime();
             } catch (Exception e) {
                 try {
                     return LocalDateTime.parse(value.toString());
                 } catch (Exception ex) {
                     log.debug("Could not parse timestamp from {}, using default", value);
                 }
             }
        }
        return defaultValue;
    }
}
