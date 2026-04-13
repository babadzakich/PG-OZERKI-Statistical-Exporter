package ru.nsu.datagen.dataGenerator.model;

import java.io.Reader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.stream.Collectors;

import com.opencsv.bean.CsvToBeanBuilder;

import lombok.extern.slf4j.Slf4j;

@Slf4j
public class TableMetadataMaker {
    public static Map<String, TableMetadata> processTableMetadata(Reader statReader, Reader constrReader) {
        // Читаем CSV и превращаем его в список объектов ColumnMetadataCSV
        List<ColumnMetadataCSV> csvData = new CsvToBeanBuilder<ColumnMetadataCSV>(statReader)
                .withType(ColumnMetadataCSV.class)
                .withSeparator(',')
                .withIgnoreLeadingWhiteSpace(true)
                .withEscapeChar('\0')
                .build()
                .parse();
        List<ConstraintCSV> constrData = new CsvToBeanBuilder<ConstraintCSV>(constrReader)
                .withType(ConstraintCSV.class)
                .withSeparator(',')
                .withIgnoreLeadingWhiteSpace(true)
                .withEscapeChar('\0')
                .build()
                .parse();
        Map<String, List<List<String>>> compositeUniquePeersMap = new HashMap<>();
        Map<String, List<List<String>>> compositeFkPeersMap = new HashMap<>();
        constrData.forEach(constraint -> {
            String[] columns = constraint.getColumns().split(",");
            if (constraint.getType().equalsIgnoreCase("UNIQUE") || constraint.getType().equalsIgnoreCase("PK")) {
                for (String column : columns) {
                    compositeUniquePeersMap.computeIfAbsent(column.trim(), k -> new ArrayList<>()).add(
                            Arrays.stream(columns)
                                    .map(String::trim)
                                    .filter(col -> !col.equals(column.trim()))
                                    .collect(Collectors.toList())
                    );
                }
            } else if (constraint.getType().equalsIgnoreCase("FK")) {
                for (String column : columns) {
                    compositeFkPeersMap.computeIfAbsent(column.trim(), k -> new ArrayList<>()).add(
                            Arrays.stream(columns)
                                    .map(String::trim)
                                    .filter(col -> !col.equals(column.trim()))
                                    .collect(Collectors.toList())
                    );
                }
            }
        }

        );

        var result =  csvData.stream()
                .collect(Collectors.groupingBy(ColumnMetadataCSV::getTableName)) // Группируем по имени таблицы наши колонки из CSV
                .entrySet().stream() // Тут мы получаем поток, где каждый энтри - это имя таблицы и список цсв колонок, относящихся к этой таблице
                .map(entry -> { // Тут мы мапаем каждую группу колонок в объект TableMetadata
                    String tableName = entry.getKey();
                    List<ColumnMetadataCSV> tableCsvColumns = entry.getValue();

                    Map<String, ColumnMetadata> columnMetadataMap = tableCsvColumns.stream() // Тут мы превращаем список колонок из CSV в мапу, где ключ - имя колонки, а значение - объект ColumnMetadata
                            .map(col ->
                                    col.transformToColumnMetadata(
                                            compositeUniquePeersMap.getOrDefault(
                                                col.getSchemaName() + "." + col.getTableName() + "." + col.getColumnName(), Collections.emptyList()
                                            ),
                                            compositeFkPeersMap.getOrDefault(
                                                    col.getSchemaName() + "." + col.getTableName() + "." + col.getColumnName(), Collections.emptyList()
                                            )
                                    ))
                            .collect(Collectors.toMap(ColumnMetadata::getName, column -> column));

                    int recordCount = tableCsvColumns.isEmpty() ? 0 : tableCsvColumns.getFirst().getRecordCount();
                    String namespace = tableCsvColumns.isEmpty() ? "public" : tableCsvColumns.getFirst().getSchemaName();
                    return new TableMetadata(tableName, columnMetadataMap, recordCount, namespace);
                })
                .collect(Collectors.toMap( tableMetadata -> tableMetadata.getNamespace() + "." + tableMetadata.getTableName(), table -> table)); // Тут мы превращаем поток TableMetadata в мапу, где ключ - имя таблицы, а значение - объект TableMetadata;
        result.forEach((tableName, tableMetadata) -> {
            Map<String, ColumnMetadata> columns = tableMetadata.getColumns();
            columns.forEach((columnName, columnMetadata) -> {
                if (columnMetadata.isForeignKey()) {
                    columnMetadata.getForeignKeyMetadata().removeIf(ref -> {
                        log.debug("Processing foreign key column {}.{}.{}", tableName, columnName, ref);
                        String refTableKey = ref.getReferencedSchema() + "." + ref.getReferencedTable();
                        log.debug("Ref table key {} {}", tableName, refTableKey);
                        if (result.containsKey(refTableKey)) {
                            log.debug("Table already exists in table {} {}", tableName, refTableKey);
                            var refTable = result.get(refTableKey);
                            if (refTable.getNamespace().equals(ref.getReferencedSchema())
                                    && refTable.getColumns().get(ref.getReferencedColumn()) != null) {
                                return false;
                            }
                        }
                        log.warn("Removing foreign key reference from {}.{} to {}.{}.{} because referenced table or column does not exist",
                                tableName, columnName, ref.getReferencedSchema(), ref.getReferencedTable(), ref.getReferencedColumn());
                        return true;
                    });
                    if (columnMetadata.getForeignKeyMetadata().isEmpty()) {
                        log.warn("Column {}.{}.{} is marked as foreign key but has no valid references. Marking as non-foreign key.",
                                tableName, columnName, columnMetadata.getName());
                        columnMetadata.setForeignKey(false);
                    }
                }
            });
            Set<String> actualRefTables = columns.values().stream()
                    .filter(ColumnMetadata::isForeignKey)
                    .map(ColumnMetadata::getForeignKeyMetadata)
                    .filter(Objects::nonNull)
                    .flatMap(Collection::stream)
                    .map(fk -> fk.getReferencedSchema() + "." + fk.getReferencedTable())
                    .collect(Collectors.toSet());
            tableMetadata.setRefTables(actualRefTables);
        });
        return result;
    }
}