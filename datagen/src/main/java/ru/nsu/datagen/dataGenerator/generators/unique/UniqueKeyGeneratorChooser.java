package ru.nsu.datagen.dataGenerator.generators.unique;

import java.util.List;
import java.util.Map;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.GeneratorsTypes;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.MarkovGenerator;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.SimpleUniqueGenerator;

public class UniqueKeyGeneratorChooser {
    public static void generate(List<ColumnMetadata> uniqColumns, Map<String, List<Object>> columnData,
                                GeneratorsTypes type, int recordCount,
                                Map<String, List<ReferencingTreeNode>> referencingTrees,
                                Map<String, List<Object>> allGeneratedData, Map<String, List<Object>> referencedData) {
        UniqueKeyGenerator generator = switch (type) {
            case MARKOV -> new MarkovGenerator(uniqColumns, recordCount, referencingTrees, allGeneratedData, referencedData);
            case SIMPLE -> new SimpleUniqueGenerator(uniqColumns, recordCount, referencingTrees, allGeneratedData, referencedData);
        };
        generator.generate(columnData);
    }
}