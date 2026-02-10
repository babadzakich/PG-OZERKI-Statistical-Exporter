package ru.nsu.datagen.dataGenerator.generators.numbergenerator.geoma;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;
import org.postgresql.geometric.PGpoint;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public class PointValueGenerator implements ValueGenerator {
    private final Faker faker = new Faker();
    @Override
    public Object generateValue() {
        return new PGpoint(faker.random().nextDouble(), faker.random().nextDouble());
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        if (leftBorder instanceof PGpoint left && rightBorder instanceof PGpoint right) {
            double x = faker.random().nextDouble(left.x, right.x);
            double y = faker.random().nextDouble(left.y, right.y);
            return new PGpoint(x, y);
        } else {
            log.warn("Invalid border types for {} value generation: {} and {}, using default value generator",
                    this.getClass(), leftBorder.getClass(), rightBorder.getClass());
            return generateValue();
        }
    }

    @Override
    public List<Object> generateValues(int count) {
        return generateValues(count, new PGpoint(-1000, -1000), new PGpoint(1000, 1000));
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        Set<PGpoint> uniqueValues = new HashSet<>();
        long maxAttempts = count * 100L;
        long attempts = 0;

        while (uniqueValues.size() < count && attempts < maxAttempts) {
            uniqueValues.add((PGpoint) generateValue(leftBorder, rightBorder));
            attempts++;
        }

        if (uniqueValues.size() < count) {
            log.warn("Could only generate {} unique values out of requested {}, consider increasing the range or allowing duplicates",
                    uniqueValues.size(), count);
        }

        return new ArrayList<>(uniqueValues);
    }
}
