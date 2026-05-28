package ru.nsu.datagen.dataGenerator.generators.numbergenerator.geoma;

import lombok.extern.slf4j.Slf4j;
import net.datafaker.Faker;
import org.postgresql.geometric.PGpoint;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorAC;

@Slf4j
public class PointValueGenerator extends ValueGeneratorAC {
    private final Faker faker = new Faker();

    public PointValueGenerator() {
        super(new PGpoint(-1000, -1000), new PGpoint(1000, 1000));
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
}
