package ru.nsu.datagen.dataGenerator.generators;

import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.normal.NormalValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.normal.StatTypeBasedGenerator;
import ru.nsu.datagen.dataGenerator.generators.pk.PrimaryKeyGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGeneratorChooser;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.GeneratorsTypes;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;

import java.util.*;

public class DataGenerator {
    private final PrimaryKeyGeneratorFactory pkGeneratorFactory;
    private final ForeignKeyGeneratorFactory fkGeneratorFactory;
    private final NormalValueGenerator normalValueGenerator;

    public DataGenerator() {
        this.pkGeneratorFactory = new PrimaryKeyGeneratorFactory();
        this.fkGeneratorFactory = new ForeignKeyGeneratorFactory();
        this.normalValueGenerator = new StatTypeBasedGenerator();
    }

    /**
     * Генерирует данные для таблицы
     * @param table метаданные таблицы
     * @param existingData уже сгенерированные данные в формате: tableName -> columnName -> List<значений>
     * @return сгенерированные данные для таблицы в формате: columnName -> List<значений>
     */
    public Map<String, List<Object>> generateTableData(
            TableMetadata table,
            Map<String, Map<String, List<Object>>> existingData) {

        // Собираем referenced данные для FK
        Map<String, List<Object>> referencedData = collectReferencedData(table, existingData);

        // Генерируем данные для каждой колонки
        return generateColumnData(table, existingData, referencedData);
    }

    /**
     * Собирает данные из таблиц, на которые ссылаются внешние ключи
     */
    private Map<String, List<Object>> collectReferencedData(
            TableMetadata table,
            Map<String, Map<String, List<Object>>> existingData) {

        Map<String, List<Object>> referencedData = new HashMap<>();

        for (ColumnMetadata column : table.getColumns().values()) {
            if (column.isForeignKey()) {
                String refTable = column.getForeignKeyMetadata().getReferencedTable();
                String refColumn = column.getForeignKeyMetadata().getReferencedColumn();

                if (existingData.containsKey(refTable)) {
                    Map<String, List<Object>> refTableData = existingData.get(refTable);
                    if (refTableData.containsKey(refColumn)) {
                        // Формируем ключ в формате "table.column"
                        String refKey = refTable + "." + refColumn;
                        referencedData.put(refKey, refTableData.get(refColumn));
                    } else {
                        System.err.println("Warning: Referenced column '" + refColumn +
                                "' not found in table '" + refTable + "'");
                    }
                } else {
                    System.err.println("Warning: Referenced table '" + refTable + "' not found in generated data");
                }
            }
        }

        return referencedData;
    }

    /**
     * Извлекает значения конкретной колонки из данных таблицы
     */
    private List<Object> extractColumnValues(
            Map<String, List<Object>> tableData,
            String columnName) {

        return tableData.getOrDefault(columnName, new ArrayList<>());
    }

    /**
     * Генерирует данные для всех колонок таблицы
     */
    private Map<String, List<Object>> generateColumnData(
            TableMetadata table,
            Map<String, Map<String, List<Object>>> existingData,
            Map<String, List<Object>> referencedData) {

        Map<String, List<Object>> columnData = new HashMap<>();
        Set<String> generatedColumns = new HashSet<>();

        // Сначала генерируем FK, потом PK, потом обычные колонки потом уники
        generateForeignKeys(table, columnData, existingData, referencedData, generatedColumns);
        generatePrimaryKeys(table, columnData, generatedColumns);
        generateNormalColumns(table, columnData, generatedColumns);
        generateUniqueConstraint(table, columnData, generatedColumns);

        return columnData;
    }

    /**
     * Генерирует первичные ключи
     */
    private void generatePrimaryKeys(
            TableMetadata table,
            Map<String, List<Object>> columnData,
            Set<String> generatedColumns) {

        for (ColumnMetadata column : table.getColumns().values()) {
            if (column.isPrimaryKey() && !generatedColumns.contains(column.getName())) {
                List<Object> primaryKeys = pkGeneratorFactory.getGenerator(column).generatePrimaryKeys(column);
                columnData.put(column.getName(), primaryKeys);
                generatedColumns.add(column.getName());
            }
        }
    }

    /**
     * Генерирует обычные колонки (не PK, не FK)
     */
    private void generateNormalColumns(
            TableMetadata table,
            Map<String, List<Object>> columnData,
            Set<String> generatedColumns) {

        for (ColumnMetadata column : table.getColumns().values()) {
            if (!column.isPrimaryKey() && !column.isForeignKey() && !column.isUnique()) {
                List<Object> values = normalValueGenerator.generateValues(column);
                columnData.put(column.getName(), values);
                generatedColumns.add(column.getName());
            }
        }
    }

    /**
     * Генерирует внешние ключи
     */
    private void generateForeignKeys(
            TableMetadata table,
            Map<String, List<Object>> columnData,
            Map<String, Map<String, List<Object>>> existingData,
            Map<String, List<Object>> referencedData,
            Set<String> generatedColumns) {

        for (ColumnMetadata column : table.getColumns().values()) {
            if (column.isForeignKey()) {
                List<Object> foreignKeys = fkGeneratorFactory.getGenerator(column)
                        .generateForeignKeys(column, referencedData, existingData);
                columnData.put(column.getName(), foreignKeys);
                generatedColumns.add(column.getName());
            }
        }
    }

    private void generateUniqueConstraint(
        TableMetadata table,
        Map<String, List<Object>> columnData,
        Set<String> generatedColumns) {
            List<ColumnMetadata> uniqueList = new ArrayList<>();
            for (ColumnMetadata column : table.getColumns().values()) {
                if (column.isUnique() && !generatedColumns.contains(column.getName())) {
                    uniqueList.add(column);
                }
            }
            if (!uniqueList.isEmpty())
                UniqueKeyGeneratorChooser.generate(uniqueList, columnData, uniqueList.size() > 1 ? GeneratorsTypes.MARKOV : GeneratorsTypes.SIMPLE, table.getRecordCount());
            uniqueList.forEach(column -> generatedColumns.add(column.getName()));
    }
}