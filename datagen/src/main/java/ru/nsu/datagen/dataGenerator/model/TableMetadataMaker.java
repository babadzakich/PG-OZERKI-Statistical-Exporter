package ru.nsu.datagen.dataGenerator.model;

import ru.nsu.datagen.dataGenerator.generators.fk.RelationshipType;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class TableMetadataMaker {
    //private Map<String, List<String[]>> columnDataGroupedByTablename;
   //private Map<String, List<String>> tableToColumnNames;

//    public void TableMetadataMaker(List<String[]> rawData) {
//        columnDataGroupedByTablename = new HashMap<>();
//
//        for(String[] line : rawData) {
//            if (!columnDataGroupedByTablename.containsKey(line[1])) {
//                columnDataGroupedByTablename.put(line[1], new ArrayList<>());
//            }
//            columnDataGroupedByTablename.get(line[1]).add(line);
//        }
//        processRawTableMetadata();
//    }

    public static List<TableMetadata> processRawTableMetadata(List<String[]> rawData) {
        Map<String, List<ColumnMetadata>> columnDataGroupedByTablename = new HashMap<>();

        for (String[] line : rawData) {
            ForeignKeyMetadata fkMetadata = line[6].contains("FK")
                    ? new ForeignKeyMetadata(line[9], line[10], line[9].equals("NULL") ? null : RelationshipType.valueOf(line[8]))
                    : null;
            ColumnMetadata columnMetadata = new ColumnMetadata(
                    line[2], line[3], line[6].contains("PK"), line[6].contains("FK"),
                    line[6].startsWith("PK") || line[6].startsWith("UNIQUE"),
                    Integer.getInteger(line[5]) == null ? 0 : Integer.parseInt(line[5]),
                    Integer.parseInt(line[4]) == -1 ? 0 : Integer.parseInt(line[4]),
                    Integer.parseInt(line[7]),
                    fkMetadata
            );
            if (!columnDataGroupedByTablename.containsKey(line[1])) {
                columnDataGroupedByTablename.put(line[1], new ArrayList<>());
            }
            columnDataGroupedByTablename.get(line[1]).add(columnMetadata);
        }

        List<TableMetadata> tableMetadataList = new ArrayList<>();
        for (String tableName : columnDataGroupedByTablename.keySet()) {
            Map<String, ColumnMetadata> columnMetadataMap = new HashMap<>();
            columnDataGroupedByTablename.get(tableName).forEach(
                    columnMetadata -> columnMetadataMap.put(columnMetadata.getName(), columnMetadata)
            );
            tableMetadataList.add(
                    new TableMetadata(
                            tableName,
                            columnMetadataMap,
                            columnDataGroupedByTablename.get(tableName).get(0).getRecordCount()
                    )
            );
        }

        return tableMetadataList;
    }

}
