package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.time.LocalDate;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.sql.Timestamp;

import net.datafaker.Faker;
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
        columnData.put(column.getName(), new ArrayList<>());
        generate(columnData);
        return columnData.get(column.getName());
	}

	@Override
	public void generate(Map<String, List<Object>> columnData) {
        String columnName = column.getName();
        List<Object> values = columnData.get(columnName);

        switch (column.getDataType()) {
            case "integer", "bigint", "smallint" -> generateIntegerValues(values);
            case "real", "double precision", "numeric", "decimal" -> generateDoubleValues(values);
            case "timestamp", "timestamp without time zone", "timestamp with time zone" -> generateTimestampValues(values);
            case "date" -> generateDateValues(values);
            case "bytea" -> generateByteValues(values);
            default -> throw new IllegalArgumentException("Unsupported data type for SimpleUniqueGenerator: " + column.getDataType());
        }
    }

    private void generateIntegerValues(List<Object> values) {
        for (int i = 1; i <= recordCount; i++) {
            values.add(i);
        }
    }

    private void generateDoubleValues(List<Object> values) {
        for (int i = 1; i <= recordCount; i++) {
            values.add((double) i);
        }
    }

    private void generateTimestampValues(List<Object> values) {
        long start = Timestamp.valueOf("2000-01-01 00:00:00").getTime();
        for (int i = 0; i < recordCount; i++) {
            Timestamp ts = new Timestamp(start + (long)i * 1000);
            values.add(ts.toString());
        }
    }

    private void generateDateValues(List<Object> values) {
        Faker f = new Faker();
        LocalDate start = f.timeAndDate().birthday();
        for (int i = 0; i < recordCount; i++) {
            LocalDate date = start.plusDays(i);
            values.add(date.toString());
        }
    }

    private void generateByteValues(List<Object> values) {
        for (int i = 1; i <= recordCount; i++) {
            values.add(java.nio.ByteBuffer.allocate(4).putInt(i).array());
        }
    }

}
