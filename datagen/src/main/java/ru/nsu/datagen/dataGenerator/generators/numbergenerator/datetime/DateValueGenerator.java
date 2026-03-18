package ru.nsu.datagen.dataGenerator.generators.numbergenerator.datetime;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

import java.time.*;
import java.util.*;

@Slf4j
public class DateValueGenerator extends ValueGeneratorAC {
    private static final Instant PG_MIN_TIMESTAMP = LocalDateTime.of(-4712, 1, 1, 0, 0, 0).toInstant(ZoneOffset.UTC);
    private static final Instant PG_MAX_TIMESTAMP = LocalDateTime.of(294276, 12, 31, 23, 59, 59).toInstant(ZoneOffset.UTC);
    private final Faker faker = new Faker();

    public DateValueGenerator() {
        super(PG_MIN_TIMESTAMP.atZone(ZoneOffset.UTC).toLocalDate(), PG_MAX_TIMESTAMP.atZone(ZoneOffset.UTC).toLocalDate());
    }

    public Object generateValue(Object leftBorder, Object rightBorder) {
        LocalDate leftValue = getLocalDate(leftBorder, PG_MIN_TIMESTAMP.atZone(ZoneOffset.UTC).toLocalDate()),
                rightValue = getLocalDate(rightBorder, PG_MAX_TIMESTAMP.atZone(ZoneOffset.UTC).toLocalDate());

        if (leftValue.isAfter(rightValue) || leftValue.isEqual(rightValue)) {
            log.warn("Left border {} >= right border {}, using left value", leftValue, rightValue);
            return leftValue;
        }

        return faker.timeAndDate().between(
                leftValue.atStartOfDay(ZoneOffset.UTC).toInstant(),
                rightValue.atStartOfDay(ZoneOffset.UTC).toInstant()
        ).atZone(ZoneOffset.UTC).toLocalDate();
    }

    private LocalDate getLocalDate(Object value, LocalDate defaultValue) {
        LocalDate res;
        if (value instanceof LocalDate localDate) {
            res = localDate;
        } else {
            res = defaultValue;
            log.warn("Invalid date type for {} value generation: {}, using {}}", this.getClass(), value != null ? value.getClass() : "null", defaultValue.toString());
        }
        return res;
    }
}
