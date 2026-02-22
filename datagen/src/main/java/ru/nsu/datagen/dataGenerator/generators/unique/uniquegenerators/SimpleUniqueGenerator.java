package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.*;
import java.sql.Date;
import java.sql.Timestamp;

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
        this.column = uniqColumns.get(0);
        this.referencingTrees = referencingTrees.get(this.column.getName());

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
        columnData.put(columnName, generator.generateValues(recordCount));
        List<Object> values = columnData.get(columnName);
        Set<Object> uniqueValues = new HashSet<>();
        for (ReferencingTreeNode node : referencingTrees) {
            uniqueValues.addAll(node.getToAdd());
        }
        values.addAll(uniqueValues);
    }
}
