package ru.nsu.datagen.dataGenerator.model;

import java.util.HashMap;
import java.util.Map;
import java.util.Objects;

public class TableMetadata {
    private final String tableName;
    private final Map<String, ColumnMetadata> columns;
    private final int recordCount;
    //private final String namespace;

    public TableMetadata(String tableName, Map<String, ColumnMetadata> columns, int recordCount) {
        this.tableName = tableName;
        this.columns = columns;
        this.recordCount = recordCount;
        //this.namespace = namespace;
    }

    public String getTableName() { return tableName; }
    public Map<String, ColumnMetadata> getColumns() { return new HashMap<>(columns); }
    public int getRecordCount() { return recordCount; }
    //public String getNamespace() {return namespace;}

    public boolean hasForeignKeyDependencies() {
        return columns.values().stream()
                .anyMatch(col -> col.isForeignKey() && col.getForeignKeyMetadata() != null);
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        TableMetadata that = (TableMetadata) o;
        return Objects.equals(tableName, that.tableName);
    }

    @Override
    public int hashCode() {
        return Objects.hash(tableName);
    }
}