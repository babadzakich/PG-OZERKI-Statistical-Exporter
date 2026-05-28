package ru.nsu.datagen.dataGenerator.model;

import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

import lombok.Getter;
import lombok.Setter;

/**
 * Неизменяемые метаданные одной таблицы: имя, схема, число строк и все колонки.
 *
 * <p>{@code refTables} — множество полных имён ({@code schema.table}) таблиц, на которые
 * эта таблица ссылается через FK. Заполняется {@link TableMetadataMaker} после парсинга
 * constraints CSV.
 */
public class TableMetadata {
    @Getter
    private final String tableName;
    private final Map<String, ColumnMetadata> columns;
    @Getter
    private final int recordCount;
    @Getter
    private final String namespace;
    @Getter @Setter
    private Set<String> refTables; //Те на кого мы ссылаемся

    public TableMetadata(String tableName, Map<String, ColumnMetadata> columns, int recordCount, String namespace) {
        this.tableName = tableName;
        this.columns = columns;
        this.recordCount = recordCount;
        this.namespace = namespace;
    }

    /** @return защитная копия карты колонок (изменения не влияют на оригинал) */
    public Map<String, ColumnMetadata> getColumns() { return new HashMap<>(columns); }

    /** @return {@code true} если хотя бы одна колонка является FK с заполненными метаданными */
    public boolean hasForeignKeyDependencies() {
        return columns.values().stream()
                .anyMatch(col -> col.isForeignKey() && col.getForeignKeyMetadata() != null);
    }

    /** @return полное имя таблицы в формате {@code schema.tableName} */
    public String getFullName() {
        return namespace + "." + tableName;
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