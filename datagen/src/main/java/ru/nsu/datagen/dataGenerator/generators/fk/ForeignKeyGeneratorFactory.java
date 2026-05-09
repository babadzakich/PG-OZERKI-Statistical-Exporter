package ru.nsu.datagen.dataGenerator.generators.fk;

import java.util.List;
import java.util.Map;

import ru.nsu.datagen.dataGenerator.generators.ColumnGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.impl.OneToManyForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.impl.OneToOneForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

public class ForeignKeyGeneratorFactory {
    private static final ForeignKeyGeneratorFactory instance = new ForeignKeyGeneratorFactory();

    public static ForeignKeyGeneratorFactory getInstance() {
        return instance;
    }

    private ForeignKeyGeneratorFactory() {}

    public ColumnGenerator getGenerator(List<ColumnMetadata> columns, Map<String, List<Object>> allGeneratedData) {
        RelationshipType relationshipType = columns.getFirst().getForeignKeyMetadata().getFirst().getRelationshipType();
        boolean composite = columns.size() > 1;
        return switch (relationshipType) {
            case ONE_TO_MANY -> composite
                    ? new OneToManyForeignKeyGenerator(columns, allGeneratedData)
                    : new OneToManyForeignKeyGenerator(columns.getFirst(), allGeneratedData);
            case ONE_TO_ONE -> composite
                    ? new OneToOneForeignKeyGenerator(columns, allGeneratedData)
                    : new OneToOneForeignKeyGenerator(columns.getFirst(), allGeneratedData);
            default -> throw new IllegalArgumentException("No FK generator for relationship type: " + relationshipType);
        };
    }
}