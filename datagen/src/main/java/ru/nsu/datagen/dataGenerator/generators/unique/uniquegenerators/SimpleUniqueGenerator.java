package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;
import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

public class SimpleUniqueGenerator implements UniqueKeyGenerator{
    private final ColumnMetadata column;
    private final int recordCount;
    private final List<ReferencingTreeNode> referencingTrees;
    private final Map<String, List<Object>> allGeneratedData;

    public SimpleUniqueGenerator(List<ColumnMetadata> uniqColumns, int recordCount,
                                 Map<String, List<ReferencingTreeNode>> referencingTrees,
                                 Map<String, List<Object>> allGeneratedData) {
        this.column = uniqColumns.getFirst();
        this.referencingTrees = referencingTrees == null ? null : referencingTrees.get(this.column.getName());
        this.recordCount = recordCount;
        this.allGeneratedData = allGeneratedData;
    }

	@Override
	public List<Object> generate() {
        if (column.isForeignKey()) {
            return ForeignKeyGeneratorFactory.getInstance()
                    .getGenerator(List.of(column), allGeneratedData)
                    .generateValues(column.getRecordCount(), new StateData(column))
                    .getFirst();
        }
		Map<String, List<Object>> columnData = new HashMap<>();
        generate(columnData);
        return columnData.get(column.getName());
	}

	@Override
	public void generate(Map<String, List<Object>> columnData) {
        if (column.isForeignKey()) {
            columnData.put(column.getName(),
                    ForeignKeyGeneratorFactory.getInstance()
                            .getGenerator(List.of(column), allGeneratedData)
                            .generateValues(column.getRecordCount(), new StateData(column))
                            .getFirst());
            return;
        }
        String columnName = column.getName();

        Set<Object> referencedValues = new HashSet<>(column.getMcv().keySet());
        if (referencingTrees != null) {
            for (ReferencingTreeNode node : referencingTrees) {
                collectReferencedValues(referencedValues, node);
            }
        }

        if (column.getNullCount() > 0) {
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

    @Override
    public void generate(Map<String, List<Object>> columnData, int offset, int batchSize) {
        // TODO Auto-generated method stub
        throw new UnsupportedOperationException("Unimplemented method 'generate'");
    }

    @Override
    public List<List<Object>> generateValues(int batchSize, StateData stateData) {
        List<Object> values = generate();
        int offset = stateData.getGeneratedCount();
        int end = Math.min(offset + batchSize, values.size());
        List<Object> batch = offset >= end ? List.of() : new ArrayList<>(values.subList(offset, end));
        stateData.advance(batch.size());
        return List.of(batch);
    }
}
