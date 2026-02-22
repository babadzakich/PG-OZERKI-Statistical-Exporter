package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.*;

import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;

public class SimpleUniqueGenerator implements UniqueKeyGenerator{
    private final ColumnMetadata column;
    private final int recordCount;
    private final List<ReferencingTreeNode> referencingTrees;

    public SimpleUniqueGenerator(List<ColumnMetadata> uniqColumns, int recordCount,
                                 Map<String, List<ReferencingTreeNode>> referencingTrees) {
        this.column = uniqColumns.getFirst();
        this.referencingTrees = referencingTrees == null ? null : referencingTrees.get(this.column.getName());
        this.recordCount = recordCount;
    }

	@Override
	public List<Object> generate() {
		Map<String, List<Object>> columnData = new HashMap<>();
        generate(columnData);
        return columnData.get(column.getName());
	}

	@Override
	public void generate(Map<String, List<Object>> columnData) {
        String columnName = column.getName();
        ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(column);
        List<Object> res;
        if (referencingTrees != null) {
            Set<Object> referencedValues = new HashSet<>();
            for (ReferencingTreeNode node : referencingTrees) {
                collectReferencedValues(referencedValues, node);
            }
            res = new ArrayList<>(referencedValues);
        } else {
            res = new ArrayList<>();
        }

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
