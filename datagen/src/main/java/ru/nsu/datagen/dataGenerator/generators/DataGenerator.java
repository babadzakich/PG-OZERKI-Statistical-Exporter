package ru.nsu.datagen.dataGenerator.generators;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.normal.StatTypeBasedGenerator;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGeneratorChooser;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.GeneratorsTypes;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.MarkovGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.dataGenerator.model.batchmodel.ColumnBatchState;

@Slf4j
public class DataGenerator {
    private final ForeignKeyGeneratorFactory fkGeneratorFactory = ForeignKeyGeneratorFactory.getInstance();
    private final Map<String, TableMetadata> allTablesMap;
    private final List<ColumnBatchState> columnBatchStates = new ArrayList<>();

    public DataGenerator(Set<TableMetadata> allTablesSet, TableMetadata mainTable) {
        allTablesMap = allTablesSet.stream().collect(HashMap::new, (m, t) -> m.put(t.getFullName(), t), HashMap::putAll);
    }

    public void rollbackToPreviousState() {
        columnBatchStates.forEach(ColumnBatchState::rollbackToPreviousState);
    }

    public Map<String, List<Object>> generateBatchTableData(
            TableMetadata table,
            Map<String, List<Object>> existingData,
            int batchSize) {
        ensureColumnBatchStates(table, existingData);
        Map<String, List<Object>> columnData = new HashMap<>();

        for (ColumnBatchState state : columnBatchStates) {
            List<List<Object>> generatedValues = state.produceBatch(batchSize);
            List<ColumnMetadata> columns = state.getColumns();

            if (generatedValues.size() != columns.size()) {
                throw new IllegalStateException(
                        "Generator returned " + generatedValues.size()
                                + " columns for state with " + columns.size() + " metadata columns"
                );
            }

            for (int i = 0; i < columns.size(); i++) {
                columnData.put(columns.get(i).getName(), generatedValues.get(i));
            }
        }

        return columnData;
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
                return generateBatchTableData(table, existingData, table.getRecordCount());
            }

    private void ensureColumnBatchStates(TableMetadata table, Map<String, List<Object>> existingData) {
        if (!columnBatchStates.isEmpty()) {
            return;
        }

        Set<String> generatedColumns = new HashSet<>();
        for (ColumnMetadata column : table.getColumns().values()) {
            if (generatedColumns.contains(column.getName())) {
                continue;
            }

            ColumnBatchState state;
            if (column.isPrimaryKey() || column.isUnique() || column.getNdistinct() == -1) {
                state = createUniqueState(table, column, existingData);
            } else if (column.isForeignKey()) {
                state = createForeignKeyState(table, column, existingData);
            } else {
                state = new ColumnBatchState(new StatTypeBasedGenerator(column), column);
            }

            columnBatchStates.add(state);
            state.getColumns().stream().map(ColumnMetadata::getName).forEach(generatedColumns::add);
        }
    }

    private ColumnBatchState createUniqueState(
            TableMetadata table,
            ColumnMetadata column,
            Map<String, List<Object>> existingData) {
        List<ColumnMetadata> uniqueColumns = resolvePeers(table, column, column.getCompositeUniquePeers());
        log.info("Creating batch state for unique columns: {}", uniqueColumns.stream().map(ColumnMetadata::getName).toList());

        Map<String, List<ReferencingTreeNode>> referencingTrees = new HashMap<>();
        for (ColumnMetadata uniqueColumn : uniqueColumns) {
            referencingTrees.put(uniqueColumn.getName(), collectReferencingTree(uniqueColumn, new HashSet<>()));
        }

        if (uniqueColumns.size() > 1) {
            return new ColumnBatchState(
                    new MarkovGenerator(uniqueColumns, table.getRecordCount(), referencingTrees, existingData),
                    uniqueColumns
            );
        }

        Map<String, List<Object>> precomputedData = new HashMap<>();
        UniqueKeyGeneratorChooser.generate(
                uniqueColumns,
                precomputedData,
                GeneratorsTypes.SIMPLE,
                table.getRecordCount(),
                referencingTrees,
                existingData
        );

        return new ColumnBatchState(
                new PrecomputedColumnGenerator(valuesForColumns(uniqueColumns, precomputedData)),
                uniqueColumns
        );
    }

    private ColumnBatchState createForeignKeyState(
            TableMetadata table,
            ColumnMetadata column,
            Map<String, List<Object>> existingData) {
        List<ColumnMetadata> foreignKeyColumns = resolvePeers(table, column, column.getCompositeForeignPeers());
        ColumnGenerator generator = fkGeneratorFactory.getGenerator(foreignKeyColumns, existingData);
        if (foreignKeyColumns.size() == 1) {
            return new ColumnBatchState(generator, foreignKeyColumns.getFirst());
        }
        return new ColumnBatchState(generator, foreignKeyColumns);
    }

    private List<List<Object>> valuesForColumns(
            List<ColumnMetadata> columns,
            Map<String, List<Object>> generatedData) {
        return columns.stream()
                .map(column -> {
                    List<Object> values = generatedData.get(column.getName());
                    if (values == null) {
                        throw new IllegalStateException("No generated values for column " + column.getName());
                    }
                    return values;
                })
                .toList();
    }

    private List<ColumnMetadata> resolvePeers(
            TableMetadata table,
            ColumnMetadata column,
            List<List<String>> peerGroups) {
        if (peerGroups == null || peerGroups.isEmpty() || peerGroups.getFirst().isEmpty()) {
            return List.of(column);
        }

        List<ColumnMetadata> columns = peerGroups.getFirst().stream()
                .map(peer -> {
                    String[] parts = peer.split("\\.");
                    return table.getColumns().get(parts[parts.length - 1]);
                })
                .filter(Objects::nonNull)
                .toList();

        return columns.isEmpty() ? List.of(column) : columns;
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
