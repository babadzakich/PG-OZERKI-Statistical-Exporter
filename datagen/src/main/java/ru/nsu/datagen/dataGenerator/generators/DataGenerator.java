package ru.nsu.datagen.dataGenerator.generators;

import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.normal.NormalValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.normal.StatTypeBasedGenerator;
import ru.nsu.datagen.dataGenerator.generators.pk.PrimaryKeyGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGeneratorChooser;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.GeneratorsTypes;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;

import java.util.*;

public class DataGenerator {
    private final PrimaryKeyGeneratorFactory pkGeneratorFactory;
    private final ForeignKeyGeneratorFactory fkGeneratorFactory;
    private final NormalValueGenerator normalValueGenerator;
    private final Map<String, TableMetadata> allTablesMap;

    public DataGenerator(Map<String, TableMetadata> allTablesMap) {
        this.pkGeneratorFactory = new PrimaryKeyGeneratorFactory();
        this.fkGeneratorFactory = new ForeignKeyGeneratorFactory();
        this.normalValueGenerator = new StatTypeBasedGenerator();
        this.allTablesMap = allTablesMap;
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
                for (int i = 0; i < column.getForeignKeyMetadata().size(); i++) {
                    String refSchema = column.getForeignKeyMetadata().get(i).getReferencedSchema();
                    String refTable = column.getForeignKeyMetadata().get(i).getReferencedTable();
                    String refColumn = column.getForeignKeyMetadata().get(i).getReferencedColumn();

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
        generateUniqueConstraint(table, columnData, generatedColumns);
        generateForeignKeys(table, columnData, existingData, referencedData, generatedColumns);
        generatePrimaryKeys(table, columnData, generatedColumns);
        generateNormalColumns(table, columnData, generatedColumns);

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
                    for (String col : column.getCompositeUniquePeers().getFirst()) {
                        uniqueList.add(table.getColumns().get(col));
                    }
                    break;
                }
            }

            // Собираем дерево обратных зависимостей для каждой unique-колонки
            Map<String, List<ReferencingTreeNode>> referencingTrees = new HashMap<>();
            for (ColumnMetadata uniqueCol : uniqueList) {
                List<ReferencingTreeNode> tree = collectReferencingTree(uniqueCol, new HashSet<>());
                referencingTrees.put(uniqueCol.getName(), tree);
            }

            if (!uniqueList.isEmpty())
                UniqueKeyGeneratorChooser.generate(uniqueList, columnData,
                        uniqueList.size() > 1 ? GeneratorsTypes.MARKOV : GeneratorsTypes.SIMPLE,
                        table.getRecordCount(), referencingTrees);
            uniqueList.forEach(column -> generatedColumns.add(column.getName()));
    }

    /**
     * Рекурсивно собирает дерево обратных зависимостей (incoming references) для колонки.
     * Для каждой колонки, которая ссылается на текущую через FK, проверяет —
     * ссылается ли кто-то на неё саму, и если да — рекурсивно добавляет в дерево.
     *
     * @param column  колонка, для которой собираем обратные зависимости
     * @param visited множество посещённых ключей (schema.table.column) для защиты от циклов
     * @return список корневых узлов дерева обратных зависимостей
     */
    private List<ReferencingTreeNode> collectReferencingTree(ColumnMetadata column, Set<String> visited) {
        List<ReferencingTreeNode> nodes = new ArrayList<>();

        Map<String, Map<String, List<String>>> refs = column.getReferencingColumns();
        if (refs == null || refs.isEmpty()) {
            return nodes;
        }

        for (Map.Entry<String, Map<String, List<String>>> schemaEntry : refs.entrySet()) {
            String schema = schemaEntry.getKey();
            for (Map.Entry<String, List<String>> tableEntry : schemaEntry.getValue().entrySet()) {
                String tableName = tableEntry.getKey();
                for (String colName : tableEntry.getValue()) {
                    String key = schema + "." + tableName + "." + colName;
                    if (visited.contains(key)) {
                        continue;
                    }
                    visited.add(key);

                    TableMetadata refTable = allTablesMap.get(tableName);
                    if (refTable == null) {
                        continue;
                    }

                    ReferencingTreeNode node = new ReferencingTreeNode(
                            schema, tableName, colName, refTable.getRecordCount(), refTable.getColumns().get(colName).getMcv().keySet());

                    // Рекурсивно ищем тех, кто ссылается на ссылающуюся колонку
                    ColumnMetadata refColumn = refTable.getColumns().get(colName);
                    if (refColumn != null) {
                        List<ReferencingTreeNode> children = collectReferencingTree(refColumn, visited);
                        children.forEach(node::addChild);
                    }

                    nodes.add(node);
                }
            }
        }

        return nodes;
    }
}