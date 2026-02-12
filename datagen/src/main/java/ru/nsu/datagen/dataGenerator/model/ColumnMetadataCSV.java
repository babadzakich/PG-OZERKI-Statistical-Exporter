package ru.nsu.datagen.dataGenerator.model;

import com.opencsv.bean.CsvBindByName;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.RelationshipType;

import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
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

    public ColumnMetadata transformToColumnMetadata() {
        boolean isFk = false, isPk = false, isUnique = false;
        if (modifiers != null) {
            String[] mods = modifiers.split(" ");
            Set<String> modSet = Arrays.stream(mods).map(String::trim).collect(Collectors.toSet());
            isFk = modSet.contains("FK");
            isPk = modSet.contains("PK");
            isUnique = modSet.contains("UNIQUE") && !isPk;
        }

        String refTable = isFk ? null : referencedTable;
        String refCol = isFk ? null : referencedColumn;

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

    private List<String> getStrings(String cleanedString) {
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
