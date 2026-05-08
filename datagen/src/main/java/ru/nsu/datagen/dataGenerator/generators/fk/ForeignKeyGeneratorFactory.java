package ru.nsu.datagen.dataGenerator.generators.fk;

import java.util.EnumMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

import ru.nsu.datagen.dataGenerator.generators.fk.impl.OneToManyForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.impl.OneToOneForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

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
    }

    public ForeignKeyGenerator getGenerator(List<ColumnMetadata> columns, Map<String, List<Object>> allGeneratedData) {
        if (columns.size() > 1) {
            throw new IllegalArgumentException("Use getComplexGenerator for composite foreign keys");
        }
        ColumnMetadata column = columns.getFirst();
        RelationshipType relationshipType = column.getForeignKeyMetadata().getFirst().getRelationshipType();
        ForeignKeyGenerator generator = null; 
        switch (relationshipType) {
            case ONE_TO_MANY -> generator = new OneToManyForeignKeyGenerator(column, allGeneratedData);
            case ONE_TO_ONE -> generator = new OneToOneForeignKeyGenerator(column, allGeneratedData);
        }

        if (generator == null) {
            throw new IllegalArgumentException("No FK generator for relationship type: " + relationshipType);
        }

        return generator;
    }

    public ComplexForeignKeyGenerator getComplexGenerator(List<ColumnMetadata> columns, Map<String, List<Object>> allGeneratedData) {
        RelationshipType relationshipType = columns.getFirst().getForeignKeyMetadata().getFirst().getRelationshipType();
        return switch (relationshipType) {
            case ONE_TO_MANY -> new OneToManyForeignKeyGenerator(columns, allGeneratedData);
            case ONE_TO_ONE -> new OneToOneForeignKeyGenerator(columns, allGeneratedData);
            default -> throw new IllegalArgumentException("No complex FK generator for relationship type: " + relationshipType);
        };
    }

    public Optional<ComplexForeignKeyGenerator> getComplexGenerator(ColumnMetadata column) {
        return Optional.ofNullable(complexGenerators.get(column.getForeignKeyMetadata().getFirst().getRelationshipType()));
    }
}
