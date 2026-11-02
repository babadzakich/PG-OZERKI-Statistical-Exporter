package ru.nsu.datagen.dataGenerator.generators.unique;

import java.util.List;
import java.util.Map;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.CartesianUniqueGenerator;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.GeneratorsTypes;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.MarkovGenerator;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.SimpleUniqueGenerator;

public class UniqueKeyGeneratorChooser {
    public static UniqueKeyGenerator create(List<ColumnMetadata> uniqColumns, GeneratorsTypes type, int recordCount,
                                            Map<String, List<ReferencingTreeNode>> referencingTrees,
                                            Map<String, List<Object>> allGeneratedData) {
        return switch (type) {
            case MARKOV -> new MarkovGenerator(uniqColumns, recordCount, referencingTrees, allGeneratedData);
            case CARTESIAN -> new CartesianUniqueGenerator(uniqColumns, recordCount, referencingTrees, allGeneratedData);
            case SIMPLE -> new SimpleUniqueGenerator(uniqColumns, recordCount, referencingTrees, allGeneratedData);
        };
    }

    public static void generate(List<ColumnMetadata> uniqColumns, Map<String, List<Object>> columnData,
                                GeneratorsTypes type, int recordCount,
                                Map<String, List<ReferencingTreeNode>> referencingTrees,
                                Map<String, List<Object>> allGeneratedData) {
        create(uniqColumns, type, recordCount, referencingTrees, allGeneratedData).generate(columnData);
    }
}