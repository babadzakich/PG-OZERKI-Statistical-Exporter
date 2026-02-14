package ru.nsu.datagen.dataGenerator.model;

import com.opencsv.bean.CsvBindByName;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.RelationshipType;

import java.util.*;
import java.util.stream.Collectors;

@Slf4j
@Data
public class ColumnMetadataCSV {
    @CsvBindByName(column = "table_schema")
    private String schemaName;
    @CsvBindByName(column = "table_name")
    private String tableName;
    @CsvBindByName(column = "column_name")
    private String columnName;
    @CsvBindByName(column = "data_type")
    private String dataType;
    @CsvBindByName(column = "modifiers")
    private String modifiers;
    @CsvBindByName(column = "null_percent")
    private double nullPercentage;
    @CsvBindByName(column = "row_count")
    private int recordCount;
    @CsvBindByName(column = "max_length")
    private int maxLength;
    @CsvBindByName(column = "relation_type")
    private String relationshipType;
    @CsvBindByName(column = "referenced_table")
    private String referencedTable;
    @CsvBindByName(column = "referenced_column")
    private String referencedColumn;
    @CsvBindByName(column = "mcv")
    private String mcv;
    @CsvBindByName(column = "mcv_frequencies")
    private String mcvFrequencies;
    @CsvBindByName(column = "avg_column_width_bytes")
    private int avgTupleSize;
    @CsvBindByName(column = "ndistinct")
    private double ndistinct;
    @CsvBindByName(column = "hbounds")
    private String hbounds;
    @CsvBindByName(column = "composite_peers")
    private String compositePeers;

    public ColumnMetadata transformToColumnMetadata() {
        log.debug("Start transforming column: {}.{}.{}", schemaName, tableName, columnName);
        boolean isFk = false, isPk = false, isUnique = false;
        if (modifiers != null) {
            String[] mods = modifiers.split(" ");
            Set<String> modSet = Arrays.stream(mods).map(String::trim).collect(Collectors.toSet());
            isFk = modSet.contains("FK");
            isPk = modSet.contains("PK");
            isUnique = modSet.contains("UNIQUE") && !isPk;
        }

        String refTable = isFk ? referencedTable : null;
        String refCol = isFk ? referencedColumn : null;

        RelationshipType relType = null;
        if (relationshipType != null && !relationshipType.isEmpty() && !relationshipType.equals("NULL")) {
            try {
                relType = RelationshipType.valueOf(relationshipType);
            } catch (IllegalArgumentException e) {
                log.warn("Unknown relationship type: {}", relationshipType);
            }
        }

        ForeignKeyMetadata fkMetadata = isFk
                ? new ForeignKeyMetadata(refTable, refCol, relType)
                : null;

        return ColumnMetadata.builder()
                .name(columnName)
                .dataType(dataType)
                .sourceDataType(dataType)
                .isPrimaryKey(isPk)
                .isForeignKey(isFk)
                .isUnique(isUnique)
                .nullPercentage(nullPercentage)
                .recordCount(recordCount)
                .maxLength(maxLength == -1 ? null : maxLength)
                .foreignKeyMetadata(fkMetadata)
                .mcv(processMCV(mcv, mcvFrequencies, dataType))
                .avgTupleSize(avgTupleSize)
                .ndistinct(ndistinct)
                .histogramm(parsePgArrayString(hbounds, dataType))
                .build();
    }

    private Map<Object, Double> processMCV(String rawMCVArray, String rawMCFArray, String dataType) {
        log.debug("Processing MCV for column: {}.{}.{}", schemaName, tableName, columnName);
        List<Object> processedMCV = parsePgArrayString(rawMCVArray, dataType);
        log.debug("Processed MCV values: {}", processedMCV);
        List<Double> processedMCF = parseMCFArray(rawMCFArray);
        log.debug("Processed MCF values: {}", processedMCF);

        Map<Object, Double> resultDistribution = new HashMap<>();

        for (int i = 0; i < processedMCV.size(); i++) {
            resultDistribution.put(processedMCV.get(i), processedMCF.get(i));
        }

        return resultDistribution;
    }

    /**
     * Парсит массив частот PostgreSQL (например, "{0.5,0.3,0.2}") в список Double.
     */
    private List<Double> parseMCFArray(String arrayString) {
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
    private List<Object> parsePgArrayString(String arrayString, String datatype) {
        if (arrayString == null || arrayString.isEmpty() || "{}".equals(arrayString) || arrayString.equals("NULL")) {
            return List.of();
        }

        String cleanedString = arrayString.substring(1, arrayString.length() - 1);
        if (cleanedString.isEmpty()) {
            return List.of();
        }

        List<String> stringValues = new ArrayList<>();
        StringBuilder currentElement = new StringBuilder();
        boolean inQuote = false;
        boolean escaped = false;

        for (int i = 0; i < cleanedString.length(); i++) {
            char c = cleanedString.charAt(i);

            if (escaped) {
                currentElement.append(c);
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (inQuote) {
                if (c == '"') {
                    inQuote = false;
                } else {
                    currentElement.append(c);
                }
            } else {
                if (c == '"') {
                    inQuote = true;
                } else if (c == ',') {
                    stringValues.add(currentElement.toString());
                    currentElement.setLength(0);
                } else {
                    currentElement.append(c);
                }
            }
        }
        stringValues.add(currentElement.toString());

        // Парсим строковые значения в соответствующий тип данных
        return stringValues.stream()
                .map(str -> parseValueByType(str, datatype))
                .collect(Collectors.toList());
    }

    /**
     * Парсит строковое значение в соответствующий тип данных
     */
    private Object parseValueByType(String value, String datatype) {
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
}
