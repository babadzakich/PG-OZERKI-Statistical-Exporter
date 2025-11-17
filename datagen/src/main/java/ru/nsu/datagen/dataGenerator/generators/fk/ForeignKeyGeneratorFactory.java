package ru.nsu.datagen.dataGenerator.generators.fk;

import ru.nsu.datagen.dataGenerator.generators.fk.impl.*;
import ru.nsu.datagen.dataGenerator.generators.fk.impl.ManyToManyForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.impl.ManyToOneForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.impl.OneToManyForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.impl.OneToOneForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.EnumMap;
import java.util.Map;

public class ForeignKeyGeneratorFactory {
    private final Map<RelationshipType, ForeignKeyGenerator> generators;

    public ForeignKeyGeneratorFactory() {
        this.generators = new EnumMap<>(RelationshipType.class);
        registerDefaultGenerators();
    }

    private void registerDefaultGenerators() {
        generators.put(RelationshipType.ONE_TO_ONE, new OneToOneForeignKeyGenerator());
        generators.put(RelationshipType.ONE_TO_MANY, new OneToManyForeignKeyGenerator());
        generators.put(RelationshipType.MANY_TO_ONE, new ManyToOneForeignKeyGenerator());
        generators.put(RelationshipType.MANY_TO_MANY, new ManyToManyForeignKeyGenerator());
    }

    public void registerGenerator(RelationshipType type, ForeignKeyGenerator generator) {
        generators.put(type, generator);
    }

    public ForeignKeyGenerator getGenerator(ColumnMetadata column) {
        RelationshipType relationshipType = column.getForeignKeyMetadata().getRelationshipType();
        ForeignKeyGenerator generator = generators.get(relationshipType);

        if (generator == null) {
            throw new IllegalArgumentException("No FK generator for relationship type: " + relationshipType);
        }

        return generator;
    }
}