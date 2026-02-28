package ru.nsu.datagen.dataGenerator.graph;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ForeignKeyMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import java.util.*;

public class DependencyGraph {
    private final Map<String, TableDependency> tableNodes;
    private final Map<String, TableMetadata> allTables;

    public DependencyGraph() {
        this.tableNodes = new HashMap<>();
        this.allTables = new HashMap<>();
    }

    public void addTable(TableMetadata table) {
        allTables.put(table.getTableName(), table);
        tableNodes.put(table.getTableName(), new TableDependency(table));
    }

    public void buildDependencies() {
        for (TableMetadata table : allTables.values()) {
            TableDependency current = tableNodes.get(table.getTableName());

            table.getColumns().values().stream()
                    .filter(ColumnMetadata::isForeignKey)
                    .filter(col -> col.getForeignKeyMetadata() != null)
                    .flatMap(col -> col.getForeignKeyMetadata().stream())
                    .map(ForeignKeyMetadata::getReferencedTable)
                    .filter(allTables::containsKey)
                    .map(tableNodes::get)
                    .forEach(current::addDependency);
        }
    }

    public List<TableMetadata> getGenerationOrder() {
        List<TableMetadata> order = new ArrayList<>();
        Set<String> visited = new HashSet<>();
        Set<String> tempMarks = new HashSet<>();

        for (TableDependency node : tableNodes.values()) {
            if (!visited.contains(node.getTable().getTableName())) {
                visit(node, visited, tempMarks, order);
            }
        }

        return order;
    }

    private void visit(TableDependency node, Set<String> visited, Set<String> tempMarks, List<TableMetadata> order) {
        String tableName = node.getTable().getTableName();

        if (tempMarks.contains(tableName)) {
            throw new RuntimeException("Cyclic dependency detected involving table: " + tableName);
        }

        if (!visited.contains(tableName)) {
            tempMarks.add(tableName);

            for (TableDependency dependency : node.getDependencies()) {
                visit(dependency, visited, tempMarks, order);
            }

            tempMarks.remove(tableName);
            visited.add(tableName);
            order.add(node.getTable());
        }
    }

    public boolean hasCycles() {
        try {
            getGenerationOrder();
            return false;
        } catch (RuntimeException e) {
            return true;
        }
    }
}