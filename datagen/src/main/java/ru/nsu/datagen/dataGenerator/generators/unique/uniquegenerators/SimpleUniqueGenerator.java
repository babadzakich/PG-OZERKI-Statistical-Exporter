package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.*;

import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;

public class SimpleUniqueGenerator implements UniqueKeyGenerator{
    private final ColumnMetadata column;
    private final int recordCount;
    private final List<ReferencingTreeNode> referencingTrees;
    private final Map<String, List<Object>> allGeneratedData;
    private final Map<String, List<Object>> referencedData;

    public SimpleUniqueGenerator(List<ColumnMetadata> uniqColumns, int recordCount,
                                 Map<String, List<ReferencingTreeNode>> referencingTrees,
                                 Map<String, List<Object>> allGeneratedData, Map<String, List<Object>> referencedData) {
        this.column = uniqColumns.getFirst();
        this.referencingTrees = referencingTrees == null ? null : referencingTrees.get(this.column.getName());
        this.recordCount = recordCount;
        this.allGeneratedData = allGeneratedData;
        this.referencedData = referencedData;
    }

	@Override
	public List<Object> generate() {
        if (column.isForeignKey()) {
            return ForeignKeyGeneratorFactory.getInstance().getGenerator(column).generateForeignKeys(column, allGeneratedData, referencedData);
        }
		Map<String, List<Object>> columnData = new HashMap<>();
        generate(columnData);
        return columnData.get(column.getName());
	}

	@Override
	public void generate(Map<String, List<Object>> columnData) {
        String columnName = column.getName();

        Set<Object> referencedValues = new HashSet<>(column.getMcv().entrySet());
        if (referencingTrees != null) {
            for (ReferencingTreeNode node : referencingTrees) {
                collectReferencedValues(referencedValues, node);
            }
        }

        if (column.getNullPercentage() > 0) {
            referencedValues.add(null);
        }

        List<Object> res = new ArrayList<>(referencedValues);

        ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(column);
        if (column.getHistogramm() != null) {
            generator.generateValues(res, recordCount, column.getHistogramm().getFirst(), column.getHistogramm().getLast());
        } else {
            generator.generateValues(res, recordCount);
        }
        columnData.put(columnName, res);
    }

    private void collectReferencedValues(Set<Object> values, ReferencingTreeNode node) {
        values.addAll(node.getToAdd());
        for (ReferencingTreeNode child : node.getChildren()) {
            collectReferencedValues(values, child);
        }
    }
}
