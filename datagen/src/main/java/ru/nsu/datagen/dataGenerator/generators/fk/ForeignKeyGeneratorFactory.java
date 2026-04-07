package ru.nsu.datagen.dataGenerator.generators.fk;

import ru.nsu.datagen.dataGenerator.generators.fk.impl.OneToManyForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.impl.OneToOneForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.EnumMap;
import java.util.Map;
import java.util.Optional;

public class ForeignKeyGeneratorFactory {
    private static final ForeignKeyGeneratorFactory foreignKeyGeneratorFactory = new ForeignKeyGeneratorFactory();
    private final Map<RelationshipType, ForeignKeyGenerator> generators;
    private final Map<RelationshipType, ComplexForeignKeyGenerator> complexGenerators;

    public static ForeignKeyGeneratorFactory getInstance() {
        return foreignKeyGeneratorFactory;
    }

    private ForeignKeyGeneratorFactory() {
        this.generators = new EnumMap<>(RelationshipType.class);
        this.complexGenerators = new EnumMap<>(RelationshipType.class);
        registerDefaultGenerators();
    }

    private void registerDefaultGenerators() {
        generators.put(RelationshipType.ONE_TO_ONE, new OneToOneForeignKeyGenerator());
        generators.put(RelationshipType.ONE_TO_MANY, new OneToManyForeignKeyGenerator());

        complexGenerators.put(RelationshipType.ONE_TO_ONE, new OneToOneForeignKeyGenerator());
    }

    public ForeignKeyGenerator getGenerator(ColumnMetadata column) {
        RelationshipType relationshipType = column.getForeignKeyMetadata().getFirst().getRelationshipType();
        ForeignKeyGenerator generator = generators.get(relationshipType);

        if (generator == null) {
            throw new IllegalArgumentException("No FK generator for relationship type: " + relationshipType);
        }

        return generator;
    }

    public Optional<ComplexForeignKeyGenerator> getComplexGenerator(ColumnMetadata column) {
        return Optional.ofNullable(complexGenerators.get(column.getForeignKeyMetadata().getFirst().getRelationshipType()));
    }
}