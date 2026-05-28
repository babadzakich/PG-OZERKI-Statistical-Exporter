package ru.nsu.datagen.dataGenerator.model;

import lombok.Getter;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;

/**
 * Узел дерева обратных зависимостей (incoming references).
 * Описывает колонку из другой таблицы, которая ссылается на текущую через FK,
 * а также рекурсивно — все колонки, которые ссылаются на неё саму.
 */
@Getter
public class ReferencingTreeNode {
    private final String schemaName;
    private final String tableName;
    private final String columnName;
    private final int recordCount;
    private final Set<Object> toAdd;
    private final List<ReferencingTreeNode> children;

    public ReferencingTreeNode(String schemaName, String tableName, String columnName, int recordCount, Set<Object> toAdd) {
        this.schemaName = schemaName;
        this.tableName = tableName;
        this.columnName = columnName;
        this.recordCount = recordCount;
        this.toAdd = toAdd;
        this.children = new ArrayList<>();
    }

    public void addChild(ReferencingTreeNode child) {
        children.add(child);
    }

    @Override
    public String toString() {
        return schemaName + "." + tableName + "." + columnName +
                "(records=" + recordCount + ", children=" + children.size() + ")";
    }
}

