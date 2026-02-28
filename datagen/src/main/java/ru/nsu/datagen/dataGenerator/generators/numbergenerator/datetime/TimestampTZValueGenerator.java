package ru.nsu.datagen.dataGenerator.generators.numbergenerator.datetime;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

import java.sql.Timestamp;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.*;

@Slf4j
public class TimestampTZValueGenerator extends ValueGeneratorAC {
    private static final Instant PG_MIN_TIMESTAMP = LocalDateTime.of(-4712, 1, 1, 0, 0, 0).toInstant(ZoneOffset.UTC);
    private static final Instant PG_MAX_TIMESTAMP = LocalDateTime.of(294276, 12, 31, 23, 59, 59).toInstant(ZoneOffset.UTC);

    private final Faker faker = new Faker();

    public TimestampTZValueGenerator() {
        super(PG_MIN_TIMESTAMP, PG_MAX_TIMESTAMP);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        Instant left = convertToInstant(leftBorder, PG_MIN_TIMESTAMP);
        Instant right = convertToInstant(rightBorder, PG_MAX_TIMESTAMP);

        if (left.isAfter(right) || left.equals(right)) {
            log.warn("Left border {} >= right border {}, using left value", left, right);
            return left;
        }

        return faker.timeAndDate().between(left, right).toString();
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
