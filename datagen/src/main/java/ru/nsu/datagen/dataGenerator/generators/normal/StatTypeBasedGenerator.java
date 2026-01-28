package ru.nsu.datagen.dataGenerator.generators.normal;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.*;
import org.postgresql.geometric.PGpoint;

public class StatTypeBasedGenerator implements NormalValueGenerator {
    private final Random random = new Random();

    @Override
    public List<Object> generateValues(ColumnMetadata columnMetadata) {
        Set<Object> uniqueValues = new HashSet<>();
        List<Object> uniqvalues = new ArrayList<>();
        long requiredUniqueCount = columnMetadata.getNdistinct() < 0 
            ? (long)(Math.abs(columnMetadata.getNdistinct()) * columnMetadata.getRecordCount()) 
            : (long)columnMetadata.getNdistinct();
        
        List<Object> values = new ArrayList<>();
        int recordCount = columnMetadata.getRecordCount();
        double nullPercentage = columnMetadata.getNullPercentage();
        
        for (Object mvcValue : columnMetadata.getMvc().keySet()) {
            uniqueValues.add(mvcValue);
            double freq = columnMetadata.getMvc().get(mvcValue);
            long mvcCount = (long)(freq * recordCount);
            for (long j = 0; j < mvcCount; j++) {
                values.add(mvcValue);
            }
        }

        long mvcCount = uniqueValues.size();
        long hasNulls = nullPercentage > 0 ? 1 : 0;
        long remainingUniqueCount = requiredUniqueCount - mvcCount - hasNulls;
        
        for (long i = 0; i < remainingUniqueCount; i++) {
            Object val = generateValue(columnMetadata);
            while (!uniqueValues.add(val)) {
                val = generateValue(columnMetadata);
            }
            uniqvalues.add(val);
        }

        int nullCount = (int)(recordCount * nullPercentage);
        for (int i = 0; i < nullCount; i++) {
            values.add(null);
        }
        
        if (!uniqvalues.isEmpty()) {
            for (long i = values.size(); i < recordCount; i++) {
                values.add(uniqvalues.get(random.nextInt(uniqvalues.size())));
            }
        }

        return values;
    }

    private Object generateValue(ColumnMetadata column) {
        String dataType = column.getDataType().toLowerCase();

        return switch (dataType) {
            case "integer[]", "integer", "smallint", "int" -> random.nextInt(1000);
            case "bigint" -> random.nextLong();
            case "varchar", "text", "char" -> generateString(column);
            case "boolean" -> random.nextBoolean();
            case "decimal", "numeric" -> Math.round(random.nextDouble() * 1000 * 100.0) / 100.0;
            case "date" -> generateDate();
            case "timestamp" -> generateTimestamp();
            case "timestamp with time zone", "timestamptz" -> generateTimestampWithTimeZone();
            case "time without time zone", "time" -> generateTimeWithoutTimeZone();
            case "interval" -> generateInterval();
            case "tstzrange" -> generateTstzRange();
            case "point" -> generatePoint();
            case "jsonb", "json" -> generateJson();
            default -> {
                // always return numeric(
                if (dataType.contains("numeric(")) {
                    yield (long) random.nextInt(0, 10) / 10.0;
                }
                yield "value_" + random.nextInt(1000);
            }
        };
    }

    private String generateString(ColumnMetadata column) {
        StringBuilder sb = new StringBuilder();
        long length = column.getAvgTupleSize();
        String chars = "01234563456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        for (int i = 0; i < length; i++) {
            sb.append(chars.charAt(random.nextInt(chars.length())));
        }
        return sb.toString();
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

    private PGpoint generatePoint() {
        double x = Math.round((random.nextDouble() * 180 - 90) * 1000000.0) / 1000000.0;
        double y = Math.round((random.nextDouble() * 360 - 180) * 1000000.0) / 1000000.0;
        return new PGpoint(x, y);
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