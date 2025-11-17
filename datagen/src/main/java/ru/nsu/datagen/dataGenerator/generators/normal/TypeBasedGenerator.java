package ru.nsu.datagen.dataGenerator.generators.normal;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.*;

public class TypeBasedGenerator implements NormalValueGenerator {
    private final Random random = new Random();

    @Override
    public List<Object> generateValues(ColumnMetadata columnMetadata) {
        List<Object> values = new ArrayList<>();
        int recordCount = columnMetadata.getRecordCount();
        double nullPercentage = columnMetadata.getNullPercentage();

        for (int i = 0; i < recordCount; i++) {
            if (random.nextDouble() < nullPercentage) {
                values.add(null);
            } else {
                values.add(generateValue(columnMetadata));
            }
        }

        return values;
    }

    private Object generateValue(ColumnMetadata column) {
        String dataType = column.getDataType().toLowerCase();

        switch (dataType) {
            case "integer":
            case "int":
                return random.nextInt(1000);
            case "bigint":
                return random.nextLong();
            case "varchar":
            case "text":
            case "char":
                return generateString(column);
            case "boolean":
                return random.nextBoolean();
            case "decimal":
            case "numeric":
                return Math.round(random.nextDouble() * 1000 * 100.0) / 100.0;
            case "date":
                return generateDate();
            case "timestamp":
                return generateTimestamp();
            case "timestamp with time zone":
            case "timestamptz":
                return generateTimestampWithTimeZone();
            case "time without time zone":
            case "time":
                return generateTimeWithoutTimeZone();
            case "interval":
                return generateInterval();
            case "tstzrange":
                return generateTstzRange();
            case "point":
                return generatePoint();
            case "jsonb":
            case "json":
                return generateJson();
            default:
                return "value_" + random.nextInt(1000);
        }
    }

    private String generateString(ColumnMetadata column) {
        String base = "Value_" + random.nextInt(1000);
        Integer maxLength = column.getMaxLength();

        if (maxLength != -1 && base.length() > maxLength) {
            return base.substring(0, maxLength);
        }

        return base;
    }

    private java.sql.Date generateDate() {
        long offset = random.nextInt(365 * 2) * 24L * 60L * 60L * 1000L;
        return new java.sql.Date(System.currentTimeMillis() - offset);
    }

    private java.sql.Timestamp generateTimestamp() {
        long offset = random.nextInt(365 * 2) * 24L * 60L * 60L * 1000L;
        return new java.sql.Timestamp(System.currentTimeMillis() - offset);
    }

    private String generateTimestampWithTimeZone() {
        LocalDateTime now = LocalDateTime.now();
        LocalDateTime randomDateTime = now.minusDays(random.nextInt(730))
                .minusHours(random.nextInt(24))
                .minusMinutes(random.nextInt(60));
        return randomDateTime.format(DateTimeFormatter.ISO_LOCAL_DATE_TIME) + "Z";
    }

    private String generateTimeWithoutTimeZone() {
        LocalTime randomTime = LocalTime.of(
                random.nextInt(24),
                random.nextInt(60),
                random.nextInt(60)
        );
        return randomTime.format(DateTimeFormatter.ISO_LOCAL_TIME);
    }

    private String generateInterval() {
        int days = random.nextInt(30);
        int hours = random.nextInt(24);
        int minutes = random.nextInt(60);
        int seconds = random.nextInt(60);

        List<String> intervals = Arrays.asList(
                days + " days",
                hours + " hours",
                minutes + " minutes",
                seconds + " seconds",
                days + " days " + hours + " hours",
                hours + " hours " + minutes + " minutes",
                "P" + days + "DT" + hours + "H" + minutes + "M" + seconds + "S"
        );

        return intervals.get(random.nextInt(intervals.size()));
    }

    private String generateTstzRange() {
        LocalDateTime start = LocalDateTime.now().minusDays(random.nextInt(365));
        LocalDateTime end = start.plusDays(random.nextInt(30) + 1);

        String startStr = start.format(DateTimeFormatter.ISO_LOCAL_DATE_TIME) + "Z";
        String endStr = end.format(DateTimeFormatter.ISO_LOCAL_DATE_TIME) + "Z";

        return "[\"" + startStr + "\",\"" + endStr + "\")";
    }

    private String generatePoint() {
        double x = Math.round((random.nextDouble() * 180 - 90) * 1000000.0) / 1000000.0;
        double y = Math.round((random.nextDouble() * 360 - 180) * 1000000.0) / 1000000.0;
        return "(" + x + "," + y + ")";
    }

    private String generateJson() {
        List<String> jsonTemplates = Arrays.asList(
                "{\"id\": " + random.nextInt(1000) + ", \"name\": \"user" + random.nextInt(100) + "\", \"active\": " + random.nextBoolean() + "}",
                "{\"coordinates\": {\"x\": " + random.nextInt(100) + ", \"y\": " + random.nextInt(100) + "}, \"type\": \"point\"}",
                "{\"tags\": [\"tag" + random.nextInt(10) + "\", \"tag" + random.nextInt(10) + "\", \"tag" + random.nextInt(10) + "\"], \"count\": " + random.nextInt(50) + "}",
                "{\"metadata\": {\"created\": \"" + LocalDateTime.now().minusDays(random.nextInt(30)) + "\", \"version\": " + (random.nextDouble() * 5 + 1) + "}}",
                "{\"settings\": {\"notifications\": " + random.nextBoolean() + ", \"theme\": \"dark\"}}"
        );

        return jsonTemplates.get(random.nextInt(jsonTemplates.size()));
    }
}