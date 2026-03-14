package ru.nsu.datagen;

import lombok.Getter;

import java.util.List;

/**
 * Хранит информацию о таблице для теста
 * Имя таблицы, схема, ожидаемый размер и ограничения
 */
@Getter
public class TableHolder {
    private final String name;
    private final String schema;
    private final long size;
    private final List<List<String>> uniques;
    private final List<String> pks;
    public TableHolder(String name, String schema, long size, List<List<String>> uniques, List<String> pks) {
        this.name = name;
        this.schema = schema;
        this.size = size;
        this.uniques = uniques;
        this.pks = pks;
    }
}
