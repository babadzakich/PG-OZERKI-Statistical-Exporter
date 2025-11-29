package ru.nsu.datagen.dataGenerator.generators.unique;

import java.security.NoSuchAlgorithmException;
import java.util.List;
import java.util.Map;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.GeneratorsTypes;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.MarkovGenerator;

public class UniqueKeyGeneratorChooser {
    public void generate(List<ColumnMetadata> uniqColumns, Map<String, List<Object>> columnData, GeneratorsTypes type) {
        UniqueKeyGenerator generator;
        switch (type) {
            case MARKOV:
                generator = new MarkovGenerator(uniqColumns);
                break;
            default:
                throw NoSuchAlgorithmException("algorithm " + type + " not presented");
        }
        generator.generate(columnData);
//        generator.generate();
    }
}