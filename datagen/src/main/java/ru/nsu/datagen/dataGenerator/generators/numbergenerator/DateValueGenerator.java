package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;

import java.time.*;
import java.util.*;

@Slf4j
public class DateValueGenerator implements ValueGenerator {
    private static final Instant PG_MIN_TIMESTAMP = LocalDateTime.of(-4712, 1, 1, 0, 0, 0).toInstant(ZoneOffset.UTC);
    private static final Instant PG_MAX_TIMESTAMP = LocalDateTime.of(294276, 12, 31, 23, 59, 59).toInstant(ZoneOffset.UTC);
    private final Faker faker = new Faker();

    @Override
    public Object generateValue() {
        return faker.timeAndDate().between(PG_MIN_TIMESTAMP, PG_MAX_TIMESTAMP)
                .atZone(ZoneOffset.UTC).toLocalDate();
    }

    @Override
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
            log.warn("Invalid date type for {} value generation: {}, using {}}", this.getClass(), value.getClass(), defaultValue.toString());
        }
        return res;
    }

    @Override
    public List<Object> generateValues(int count) {
        Set<String> uniqueValues = new HashSet<>();
        int maxAttempts = count * 100;
        int attempts = 0;

        while (uniqueValues.size() < count && attempts < maxAttempts) {
            uniqueValues.add(faker.timeAndDate().birthday().toString());
            attempts++;
        }

        checkUniqueGeneration(count, uniqueValues.size(), maxAttempts);
        return new ArrayList<>(uniqueValues);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        LocalDate leftValue = getLocalDate(leftBorder, PG_MIN_TIMESTAMP.atZone(ZoneOffset.UTC).toLocalDate());
        LocalDate rightValue = getLocalDate(rightBorder, PG_MAX_TIMESTAMP.atZone(ZoneOffset.UTC).toLocalDate());

        if (leftValue.isAfter(rightValue) || leftValue.isEqual(rightValue)) {
            log.warn("Left border {} >= right border {}, generating same date {} times", leftValue, rightValue, count);
            String dateStr = leftValue.toString();
            return new ArrayList<>(Collections.nCopies(count, dateStr));
        }

        Set<String> uniqueValues = new HashSet<>();
        int maxAttempts = count * 100;
        int attempts = 0;

        while (uniqueValues.size() < count && attempts < maxAttempts) {
            String dateStr = faker.timeAndDate().between(
                    leftValue.atStartOfDay(ZoneId.systemDefault()).toInstant(),
                    rightValue.atStartOfDay(ZoneId.systemDefault()).toInstant()
            ).atZone(ZoneId.systemDefault()).toLocalDate().toString();
            uniqueValues.add(dateStr);
            attempts++;
        }

        checkUniqueGeneration(count, uniqueValues.size(), maxAttempts);
        return new ArrayList<>(uniqueValues);
    }

    private void checkUniqueGeneration(int count, int generated, int maxAttempts) {
        if (generated < count) {
            String msg = String.format("Could not generate %d unique dates after %d attempts. Generated only %d unique values.",
                    count, maxAttempts, generated);
            log.error(msg);
            throw new RuntimeException(msg);
        }
    }
}
