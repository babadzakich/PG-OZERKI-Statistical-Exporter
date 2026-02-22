package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

public class SimpleUniqueGenerator implements UniqueKeyGenerator{
    private final ColumnMetadata column;
    private final int recordCount;

    public SimpleUniqueGenerator(List<ColumnMetadata> uniqColumns, int recordCount) {
        this.column = uniqColumns.get(0);
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
    }
}
