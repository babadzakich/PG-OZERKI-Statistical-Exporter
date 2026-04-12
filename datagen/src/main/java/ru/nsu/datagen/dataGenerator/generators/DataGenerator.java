package ru.nsu.datagen.dataGenerator.generators;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.ComplexForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.normal.NormalValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.normal.StatTypeBasedGenerator;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGeneratorChooser;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.GeneratorsTypes;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;

@Slf4j
public class DataGenerator {
    private final ForeignKeyGeneratorFactory fkGeneratorFactory = ForeignKeyGeneratorFactory.getInstance();
    private final NormalValueGenerator normalValueGenerator = new StatTypeBasedGenerator();
    private final Map<String, TableMetadata> allTablesMap;

    public DataGenerator(Map<String, TableMetadata> allTablesMap) {
        this.allTablesMap = allTablesMap;
    }

    public DataGenerator(Set<TableMetadata> allTablesSet) {
        allTablesMap = allTablesSet.stream().collect(HashMap::new, (m, t) -> m.put(t.getFullName(), t), HashMap::putAll);
    }

    public Map<String, List<Object>> generateBatchTableData(
            TableMetadata table,
            Map<String, List<Object>> existingData,
            int batchSize) {
                return null;
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

        Map<String, List<Object>> columnData = new HashMap<>();
        Set<String> generatedColumns = new HashSet<>();

        // Сначала генерируем UNIQUE, потом FK, потом обычные колонки
        generateUniqueConstraint(table, columnData, existingData, generatedColumns);
        generateForeignKeys(table, columnData, existingData, generatedColumns);
        generateNormalColumns(table, columnData, generatedColumns);

        return columnData;
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
            Set<String> generatedColumns) {
        for (ColumnMetadata column : table.getColumns().values()) {
            if (column.isForeignKey() && !generatedColumns.contains(column.getName())) {
                if (!column.getCompositeForeignPeers().isEmpty()) {
                    List<ColumnMetadata> foreignKeysMultiple = column.getCompositeForeignPeers().getFirst()
                            .stream().map(foreignPeer -> table.getColumns().get(foreignPeer.split("\\.")[2]))
                            .toList();
                    Optional<ComplexForeignKeyGenerator> generator = fkGeneratorFactory.getComplexGenerator(column);
                    if (generator.isPresent()) {
                        List<List<Object>> foreignKeys = generator.get().generateForeignKeys(foreignKeysMultiple, existingData);
                        for (int i = 0; i < foreignKeysMultiple.size(); i++) {
                            columnData.put(foreignKeysMultiple.get(i).getName(), foreignKeys.get(i));
                        }
                    } else {
                        throw new IllegalArgumentException("No generator found for " + column.getName());
                    }
                } else {
                    List<Object> foreignKeys = fkGeneratorFactory.getGenerator(column).generateForeignKeys(column, existingData);
                    columnData.put(column.getName(), foreignKeys);
                }
                generatedColumns.add(column.getName());
            }
        }
    }

    private void generateUniqueConstraint (
        TableMetadata table,
        Map<String, List<Object>> columnData,
        Map<String, List<Object>> existingData,
        Set<String> generatedColumns) {
            for (ColumnMetadata column : table.getColumns().values()) {
                if ((column.isUnique() || column.isPrimaryKey()) && !generatedColumns.contains(column.getName())) {
                    List<ColumnMetadata> uniqueList = new ArrayList<>();
                    if (column.getCompositeUniquePeers() != null && !column.getCompositeUniquePeers().isEmpty()) {
                        column.getCompositeUniquePeers().getFirst().stream() // TODO: Поддержка пересекающихся уникальных ключей
                                .map(col -> table.getColumns().get(col.split("\\.")[2]))
                                .forEach(uniqueList::add);
                    } else {
                        uniqueList.add(column);
                    }
                    log.info("Generating unique key for columns: {}", uniqueList.stream().map(ColumnMetadata::getName).toList());
                    // Собираем дерево обратных зависимостей для каждой unique-колонки
                    Map<String, List<ReferencingTreeNode>> referencingTrees = new HashMap<>();
                    for (ColumnMetadata uniqueCol : uniqueList) {
                        List<ReferencingTreeNode> tree = collectReferencingTree(uniqueCol, new HashSet<>());
                        referencingTrees.put(uniqueCol.getName(), tree);
                    }

                    if (!uniqueList.isEmpty())
                        UniqueKeyGeneratorChooser.generate(uniqueList, columnData,
                                uniqueList.size() > 1 ? GeneratorsTypes.MARKOV : GeneratorsTypes.SIMPLE,
                                table.getRecordCount(), referencingTrees, existingData);
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

                    TableMetadata refTable = allTablesMap.get(schema+"."+tableName);
                    if (refTable == null) {
                        continue;
                    }
                    Set<Object> toAdd = new HashSet<>(refTable.getColumns().get(colName).getMcv().keySet());
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