package ru.nsu.datagen.dataGenerator.generators.numbergenerator.datetime;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

import java.time.LocalTime;

@Slf4j
public class TimeValueGenerator extends ValueGeneratorAC {
    private final Faker faker = new Faker();

    public TimeValueGenerator() {
        super(LocalTime.MIN, LocalTime.MAX);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        LocalTime left = leftBorder instanceof LocalTime l ? l : LocalTime.MIN;
        LocalTime right = rightBorder instanceof LocalTime r ? r : LocalTime.MAX;

        if (!left.isBefore(right)) {
            log.warn("Left time border is later than right time border, using default values");
            left = LocalTime.MIN;
            right = LocalTime.MAX;
        }

        return LocalTime.ofSecondOfDay(faker.time().between(left, right)).toString();
    }
}
