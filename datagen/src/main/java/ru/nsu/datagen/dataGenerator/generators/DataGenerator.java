package ru.nsu.datagen.dataGenerator.generators;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.ComplexForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.normal.NormalValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.normal.StatTypeBasedGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGeneratorChooser;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.GeneratorsTypes;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;

import java.util.*;

@Slf4j
public class DataGenerator {
    private final ForeignKeyGeneratorFactory fkGeneratorFactory;
    private final NormalValueGenerator normalValueGenerator;
    private final Map<String, TableMetadata> allTablesMap;

    public DataGenerator(Map<String, TableMetadata> allTablesMap) {
        this.fkGeneratorFactory = new ForeignKeyGeneratorFactory();
        this.normalValueGenerator = new StatTypeBasedGenerator();
        this.allTablesMap = allTablesMap;
    }

    /**
     * Генерирует данные для таблицы
     * @param table метаданные таблицы
     * @param existingData уже сгенерированные данные в формате: namespace.tableName.columnName -> List<значений>
     * @return сгенерированные данные для таблицы в формате: columnName -> List<значений>
     */
    public Map<String, List<Object>> generateTableData(
            TableMetadata table,
            Map<String, List<Object>> existingData) {

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
            Map<String, List<Object>> existingData) {

        Map<String, List<Object>> referencedData = new HashMap<>();

        for (ColumnMetadata column : table.getColumns().values()) {
            if (column.isForeignKey()) {
                for (int i = 0; i < column.getForeignKeyMetadata().size(); i++) {
                    String refSchema = column.getForeignKeyMetadata().get(i).getReferencedSchema();
                    String refTable = column.getForeignKeyMetadata().get(i).getReferencedTable();
                    String refColumn = column.getForeignKeyMetadata().get(i).getReferencedColumn();

                    String refKey = refSchema + "." + refTable + "." + refColumn;

                    if (existingData.containsKey(refKey)) {
                        List<Object> refTableData = existingData.get(refKey);
                        referencedData.put(refKey, refTableData);
                    } else {
                        log.error("Referenced column '{}' not found in existing data for table '{}'", refKey, table.getTableName());
                        throw new RuntimeException("Referenced column not found in existing data for column '" + refKey + "'");
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
            Map<String, List<Object>> existingData,
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
                List<Object> primaryKeys;
                if (!column.getHistogramm().isEmpty()) {
                    primaryKeys = ValueGeneratorFactory.createValueGenerator(column).generateValues(column.getRecordCount(), column.getHistogramm().getFirst(), column.getHistogramm().getLast());
                } else {
                    primaryKeys = ValueGeneratorFactory.createValueGenerator(column).generateValues(column.getRecordCount());
                }
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
            Map<String, List<Object>> existingData,
            Map<String, List<Object>> referencedData,
            Set<String> generatedColumns) {
        for (ColumnMetadata column : table.getColumns().values()) {
            if (column.isForeignKey() && !generatedColumns.contains(column.getName())) {
                if (!column.getCompositeForeignPeers().isEmpty()) {
                    List<ColumnMetadata> foreignKeysMultiple = column.getCompositeForeignPeers().getFirst()
                            .stream().map(foreignPeer -> table.getColumns().get(foreignPeer.split("\\.")[2]))
                            .toList();
                    Optional<ComplexForeignKeyGenerator> generator = fkGeneratorFactory.getComplexGenerator(column);
                    if (generator.isPresent()) {
                        List<List<Object>> foreignKeys = generator.get().generateForeignKeys(foreignKeysMultiple, referencedData, existingData);
                        for (int i = 0; i < foreignKeysMultiple.size(); i++) {
                            columnData.put(foreignKeysMultiple.get(i).getName(), foreignKeys.get(i));
                        }
                    } else {
                        throw new IllegalArgumentException("No generator found for " + column.getName());
                    }
                } else {
                    List<Object> foreignKeys = fkGeneratorFactory.getGenerator(column).generateForeignKeys(column, referencedData, existingData);
                    columnData.put(column.getName(), foreignKeys);
                }
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
                    column.getCompositeUniquePeers().getFirst().stream() // TODO: Поддержка пересекающихся уникальных ключей
                            .map(col -> table.getColumns().get(col.split("\\.")[2]))
                            .forEach(uniqueList::add);
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
                    uniqueList.forEach(column2 -> generatedColumns.add(column2.getName()));
                }
            }
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
                    Set<Object> toAdd = refTable.getColumns().get(colName).getMcv().keySet();
                    toAdd.addAll(refTable.getColumns().get(colName).getHistogramm());
                    ReferencingTreeNode node = new ReferencingTreeNode(
                            schema, tableName, colName, refTable.getRecordCount(), toAdd);

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