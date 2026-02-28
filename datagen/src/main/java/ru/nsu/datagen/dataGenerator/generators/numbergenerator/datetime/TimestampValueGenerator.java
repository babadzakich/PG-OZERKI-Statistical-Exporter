package ru.nsu.datagen.dataGenerator.generators.numbergenerator.datetime;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

import java.sql.Timestamp;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;

@Slf4j
public class TimestampValueGenerator extends ValueGeneratorAC {
    private final Faker faker = new Faker();

    private static final LocalDateTime PG_MIN_TIMESTAMP = LocalDateTime.of(-4712, 1, 1, 0, 0, 0);
    private static final LocalDateTime PG_MAX_TIMESTAMP = LocalDateTime.of(294276, 12, 31, 23, 59, 59);

    public TimestampValueGenerator() {
        super(PG_MIN_TIMESTAMP, PG_MAX_TIMESTAMP);
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
