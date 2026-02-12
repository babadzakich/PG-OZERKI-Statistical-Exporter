package ru.nsu.datagen.dataGenerator.model;

import com.opencsv.bean.CsvToBeanBuilder;
import lombok.extern.slf4j.Slf4j;

import java.io.Reader;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

@Slf4j
public class TableMetadataMaker {
    public static List<TableMetadata> processTableMetadata(Reader reader) {
        List<ColumnMetadataCSV> csvData = new CsvToBeanBuilder<ColumnMetadataCSV>(reader)
                .withType(ColumnMetadataCSV.class)
                .withSeparator(',')
                .withIgnoreLeadingWhiteSpace(true)
                .build()
                .parse();

        return csvData.stream()
                .collect(Collectors.groupingBy(ColumnMetadataCSV::getTableName))
                .entrySet().stream()
                .map(entry -> {
                    String tableName = entry.getKey();
                    List<ColumnMetadataCSV> tableCsvColumns = entry.getValue();

                    Map<String, ColumnMetadata> columnMetadataMap = tableCsvColumns.stream()
                            .map(ColumnMetadataCSV::transformToColumnMetadata)
                            .collect(Collectors.toMap(ColumnMetadata::getName, column -> column));

                    int recordCount = tableCsvColumns.isEmpty() ? 0 : tableCsvColumns.getFirst().getRecordCount();
                    String namespace = tableCsvColumns.isEmpty() ? "public" : tableCsvColumns.getFirst().getSchemaName();
                    return new TableMetadata(tableName, columnMetadataMap, recordCount, namespace);
                })
                .collect(Collectors.toList());
    }
}