package ru.nsu.datagen.dataGenerator.generators.unique;

import java.util.List;
import java.util.Map;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.GeneratorsTypes;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.MarkovGenerator;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.SimpleUniqueGenerator;

public class UniqueKeyGeneratorChooser {
    public static void generate(List<ColumnMetadata> uniqColumns, Map<String, List<Object>> columnData, GeneratorsTypes type, int recordCount) {
        UniqueKeyGenerator generator;
        switch (type) {
            case MARKOV:
                generator = new MarkovGenerator(uniqColumns, recordCount);
                break;
            case SIMPLE:
                generator = new SimpleUniqueGenerator(uniqColumns, recordCount);
            default:
                throw new IllegalArgumentException("algorithm " + type + " not presented");
        }
        generator.generate(columnData);
//        generator.generate();
    }
}