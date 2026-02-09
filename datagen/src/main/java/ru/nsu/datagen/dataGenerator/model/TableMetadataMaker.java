package ru.nsu.datagen.dataGenerator.model;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.RelationshipType;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

@Slf4j
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
            
            int recordCountValue = Integer.parseInt(line[4]) == -1 ? 0 : Integer.parseInt(line[4]);
            
            double nullPercentageValue = (line[5] == null || line[5].isEmpty() || line[5].equals("NULL")) ? 0.0 : Double.parseDouble(line[5]);
            ColumnMetadata columnMetadata = ColumnMetadata.builder()
                .name(line[2])
                .dataType(line[3])
                
                .isPrimaryKey(line[6].contains("PK"))
                .isForeignKey(line[6].contains("FK"))
                .isUnique(line[6].contains("UNIQUE"))
                
                .nullPercentage(nullPercentageValue)
                .recordCount(recordCountValue)
                .maxLength(line[7].equals("-1") ? -1 : Integer.parseInt(line[7])) // Обработка -1 для длины
                .avgTupleSize((line[13].isEmpty() || line[13].equals("NULL")) ? -1 : Integer.parseInt(line[13]))
                
                .foreignKeyMetadata(fkMetadata)
                .mcv(processMCV(line[11], line[12], line[3]))
                .ndistinct(Double.parseDouble(line[14]))
                .histogramm(parsePgArrayString(line[15], line[3]))
                .build();
            log.trace("Processed column metadata: {}", columnMetadata);
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
                            columnDataGroupedByTablename.get(tableName).getFirst().getRecordCount()
                    )
            );
        }

        return tableMetadataList;
    }

    private static Map<Object, Double> processMCV(String rawMCVArray, String rawMCFArray, String dataType) {
        List<Object> processedMCV = parsePgArrayString(rawMCVArray, dataType);
        List<Double> processedMCF = parseMCFArray(rawMCFArray);

        Map<Object, Double> resultDistribution = new HashMap<>();

        for (int i = 0; i < processedMCV.size(); i++) {
            resultDistribution.put(processedMCV.get(i), processedMCF.get(i));
        }

        return resultDistribution;
    }

    /**
     * Парсит массив частот PostgreSQL (например, "{0.5,0.3,0.2}") в список Double.
     */
    private static List<Double> parseMCFArray(String arrayString) {
        if (arrayString == null || arrayString.isEmpty() || "{}".equals(arrayString) || arrayString.equals("NULL")) {
            return List.of();
        }

        String cleanedString = arrayString.substring(1, arrayString.length() - 1);
        if (cleanedString.isEmpty()) {
            return List.of();
        }

        return Arrays.stream(cleanedString.split(","))
                .map(String::trim)
                .map(Double::parseDouble)
                .collect(Collectors.toList());
    }

    /**
     * Парсит строку массива PostgreSQL (например, "{val1, "val 2", val3}") в список объектов Java.
     */
    public static List<Object> parsePgArrayString(String arrayString, String datatype) {
        if (arrayString == null || arrayString.isEmpty() || "{}".equals(arrayString) || arrayString.equals("NULL")) {
            return List.of();
        }

        String cleanedString = arrayString.substring(1, arrayString.length() - 1);
        if (cleanedString.isEmpty()) {
            return List.of();
        }
        
        List<String> stringValues = new ArrayList<>();
        if (cleanedString.startsWith("\"")) {
            Pattern pattern = Pattern.compile("\"(.*?)\"");
            Matcher matcher = pattern.matcher(cleanedString);

            while (matcher.find()) {
                stringValues.add(matcher.group(1));
            }
        } else {
            stringValues = getStrings(cleanedString);
        }
        
        // Парсим строковые значения в соответствующий тип данных
        return stringValues.stream()
                .map(str -> parseValueByType(str, datatype))
                .collect(Collectors.toList());
    }
    
    /**
     * Парсит строковое значение в соответствующий тип данных
     */
    private static Object parseValueByType(String value, String datatype) {
        if (value == null || value.isEmpty() || value.equals("NULL")) {
            return null;
        }
        
        String lowerDatatype = datatype.toLowerCase();
        
        try {
            return switch (lowerDatatype) {
                case "smallint", "int2" -> Short.parseShort(value.trim());
                case "integer", "int4", "int" -> Integer.parseInt(value.trim());
                case "bigint", "int8" -> Long.parseLong(value.trim());
                case "real", "float4" -> Float.parseFloat(value.trim());
                case "double precision", "float8" -> Double.parseDouble(value.trim());
                case "varchar", "text", "char", "interval", "tstzrange", "point", "jsonb", "json" -> value;
                case "bool", "boolean" -> "t".equals(value.trim()) || "true".equalsIgnoreCase(value.trim());
                case "date" -> java.sql.Date.valueOf(value.trim());
                case "timestamp", "timestamp with time zone", "timestamptz" -> value.trim();
                case "time without time zone", "time" -> java.sql.Time.valueOf(value.trim());
                default -> {
                    if (lowerDatatype.contains("numeric") || lowerDatatype.contains("decimal")) {
                        yield Double.parseDouble(value.trim());
                    }
                    yield value;
                }
            };
        } catch (Exception e) {
            log.warn("Failed to parse value '{}' as type '{}', returning as String. Error: {}", value, datatype, e.getMessage());
            return value;
        }
    }

    private static List<String> getStrings(String cleanedString) {
        List<String> result = new ArrayList<>();
        String[] parts = cleanedString.split(",");

        for (int i = 0; i < parts.length; i++) {
            String part = parts[i];

            if (i == 0 && part.startsWith("\"")) {
                part = part.substring(1);
            }

            if (i == parts.length - 1 && part.endsWith("\"")) {
                part = part.substring(0, part.length() - 1);
            }

            part = part.replace("\"\"", "\"");

            result.add(part);
        }
        return result;
    }
}
