package ru.nsu.datagen.dataGenerator.generators.numbergenerator.datetime;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

import java.sql.Timestamp;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.*;

@Slf4j
public class TimestampTZValueGenerator extends ValueGeneratorAC {
    private static final Instant PG_MIN_TIMESTAMP = LocalDateTime.of(1970, 1, 1, 0, 0, 0).toInstant(ZoneOffset.UTC);
    private static final Instant PG_MAX_TIMESTAMP = LocalDateTime.of(2100, 12, 31, 23, 59, 59).toInstant(ZoneOffset.UTC);

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

        return OffsetDateTime.ofInstant(faker.timeAndDate().between(left, right), ZoneOffset.UTC);
    }

    private Instant convertToInstant(Object value, Instant defaultValue) {
        if (value instanceof Instant instant) {
            return instant;
        }
        if (value instanceof Timestamp timestamp) {
            return timestamp.toInstant();
        }

        if (value != null) {
            String valStr = value.toString();
            try {
                return Instant.parse(valStr);
            } catch (Exception e) {
                // Try customized parsing
            }

            try {
                String normalized = valStr.replace(' ', 'T');
                int signIndex = Math.max(normalized.lastIndexOf('+'), normalized.lastIndexOf('-'));
                if (signIndex > normalized.lastIndexOf('T')) {
                    String offset = normalized.substring(signIndex);
                    if (offset.length() == 3) {
                        normalized = normalized + ":00";
                    }
                }
                return Instant.parse(normalized);
            } catch (Exception e) {
                log.warn("Could not parse timestamp from '{}', using default {}", value, e.getMessage());
            }
        }
        return defaultValue;
    }
}
