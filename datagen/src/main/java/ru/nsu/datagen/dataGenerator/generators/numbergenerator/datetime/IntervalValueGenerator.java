package ru.nsu.datagen.dataGenerator.generators.numbergenerator.datetime;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;

import java.time.Duration;
import java.time.Period;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class IntervalValueGenerator implements ValueGenerator {
    private final Faker faker = new Faker();
    @Override
    public Object generateValue() {
        return generateValue("P0Y0M0DT0H0M0S", "P10Y12M30DT24H0M0S");
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
            if (leftBorder instanceof String leftStr && rightBorder instanceof String rightStr) {
                try {
                    Interval left = parseInterval(leftStr);
                    Interval right = parseInterval(rightStr);

                    Period randomPeriod = Period.of(
                            faker.number().numberBetween(left.period().getYears(), right.period().getYears() + 1),
                            faker.number().numberBetween(left.period().getMonths(), right.period().getMonths() + 1),
                            faker.number().numberBetween(left.period().getDays(), right.period().getDays() + 1)
                    );

                    Duration randomDuration = Duration.ofSeconds(faker.number().numberBetween(left.duration().getSeconds(), right.duration().getSeconds() + 1));

                    return randomPeriod.toString() + " " + randomDuration.toString().substring(1);
                } catch (IllegalArgumentException e) {
                    log.error("Failed to parse interval borders: {}", e.getMessage());
                }
            } else {
                log.warn("Invalid border types for {} value generation: {} and {}, using default values", this.getClass().toString(), leftBorder.getClass(), rightBorder.getClass());
            }
        return null;
    }

    @Override
    public List<Object> generateValues(int count) {
        return generateValues(count, "P0Y0M0DT0H0M0S", "P10Y12M30DT24H0M0S");
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
            Set<String> values = new HashSet<>();
            long maxAttempts = count * 100L;
            long attempts = 0;
            while (values.size() < count && attempts < maxAttempts) {
                values.add((String) generateValue(leftBorder, rightBorder));
                attempts++;
            }

            return new ArrayList<>(values);
    }

    private Interval parseInterval(String durationStr) {
        String[] parts = durationStr.split("T");
        if (parts.length != 2) {
            log.warn("Invalid interval format, expected PnYnMnDTnHnMnS, got {}", durationStr);
             throw new IllegalArgumentException("Invalid interval format, expected PnYnMnDTnHnMnS");
        }
        Period p = Period.parse(parts[0]);
        Duration d = Duration.parse("P" + parts[1]);
        return new Interval(p, d);
    }

    private record Interval(Period period, Duration duration) {
        @Override
        public String toString() {
            return period.toString() + " " + duration.toString().substring(1);
        }
    }
}
