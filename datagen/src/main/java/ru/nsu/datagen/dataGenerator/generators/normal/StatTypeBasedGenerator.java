package ru.nsu.datagen.dataGenerator.generators.normal;

import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.SimpleUniqueGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.postgresql.geometric.PGpoint;
import com.github.javafaker.Faker;

public class StatTypeBasedGenerator implements NormalValueGenerator {
    private final Random random = new Random();
    private final Faker faker = new Faker();

    @Override
    public List<Object> generateValues(ColumnMetadata columnMetadata) {
        if (columnMetadata.getNdistinct() == -1) {
            UniqueKeyGenerator generator = new SimpleUniqueGenerator(List.of(columnMetadata), columnMetadata.getRecordCount());
            return generator.generate();
        }
        Set<Object> objectSet = new HashSet<>();

        long requiredUniqueCount = columnMetadata.getNdistinct() < 0 
            ? (long)(Math.abs(columnMetadata.getNdistinct()) * columnMetadata.getRecordCount())
            : (long)columnMetadata.getNdistinct();
        System.err.println("Generating " + requiredUniqueCount + " unique values for column " + columnMetadata.getName() + " where ndistinct = " + columnMetadata.getNdistinct());
        List<Object> values = new ArrayList<>();
        int recordCount = columnMetadata.getRecordCount();
        System.err.println("Total record count: " + recordCount);
        double nullPercentage = columnMetadata.getNullPercentage();
        System.err.println("Null percentage: " + nullPercentage);

        System.err.println("Processing MCVs for column " + columnMetadata.getName());
        for (Object mvcValue : columnMetadata.getMcv().keySet()) {
            objectSet.add(mvcValue);
            double freq = columnMetadata.getMcv().get(mvcValue);
            long mvcCount = (long)(freq * recordCount);
            System.err.println("Adding MVC value: " + mvcValue + " with frequency: " + freq + " resulting in count: " + mvcCount);
            for (long j = 0; j < mvcCount; j++) {
                values.add(mvcValue);
            }
        }
        int nullCount = (int)(recordCount * nullPercentage);
        System.err.println("Adding nulls, count: " + nullCount);
        for (int i = 0; i < nullCount; i++) {
            values.add(null);
        }

        long mvcCount = objectSet.size();
        long hasNulls = nullPercentage > 0 ? 1 : 0;
        long remainingUniqueCount = requiredUniqueCount - mvcCount - hasNulls;
        long remainingValueCount = recordCount - values.size();
        System.err.println("Remaining unique count to generate: " + remainingUniqueCount);
        System.err.println("Remaining value count to fill: " + remainingValueCount);

        int histogramBuckets = columnMetadata.getHistogramm().size() - 1;

        if (histogramBuckets > 0 && remainingUniqueCount > 0 && remainingValueCount > 0) {
            for (int i = 0; i < histogramBuckets; i++) {
                Object lowerBound = columnMetadata.getHistogramm().get(i);
                Object upperBound = columnMetadata.getHistogramm().get(i + 1);

                // Распределяем уникальные значения равномерно по бакетам с учётом остатка
                // Например: 2002 значения на 100 бакетов = 20 + (1 если i < 2), т.е. первые 2 бакета по 21, остальные по 20
                long baseUniqueCount = remainingUniqueCount / histogramBuckets;
                long uniqueRemainder = remainingUniqueCount % histogramBuckets;
                long rangeUniqueCount = baseUniqueCount + (i < uniqueRemainder ? 1 : 0);

                // Распределяем общее количество значений равномерно по бакетам с учётом остатка
                long baseValueCount = remainingValueCount / histogramBuckets;
                long valueRemainder = remainingValueCount % histogramBuckets;
                long rangeValueCount = baseValueCount + (i < valueRemainder ? 1 : 0);

                System.err.println("Bucket " + i + ": Generating " + rangeUniqueCount + " unique values between " + lowerBound + " and " + upperBound);
                System.err.println("Bucket " + i + ": Will fill " + rangeValueCount + " total values");

                // Генерируем уникальные значения для данного бакета
                List<Object> bucketUniqueValues = new ArrayList<>();
                for (long j = 0; j < rangeUniqueCount; j++) {
                    Object val = generateValue(columnMetadata, lowerBound, upperBound);
                    int attempts = 0;
                    while (!objectSet.add(val) && attempts < 10000) {
                        val = generateValue(columnMetadata, lowerBound, upperBound);
                        attempts++;
                    }
                    if (attempts < 10000) {
                        bucketUniqueValues.add(val);
                    }
                }

                // Заполняем values случайными значениями из сгенерированных уникальных
                if (!bucketUniqueValues.isEmpty()) {
                    for (long j = 0; j < rangeValueCount; j++) {
                        values.add(bucketUniqueValues.get(random.nextInt(bucketUniqueValues.size())));
                    }
                    System.err.println("Bucket " + i + ": Added " + rangeValueCount + " values from " + bucketUniqueValues.size() + " unique values");
                } else {
                    System.err.println("Warning: Could not generate unique values for bucket " + i);
                }
            }
        }

        // Fallback: если после обработки гистограммы все еще не хватает значений
        if (values.size() < recordCount) {
            long missingValueCount = recordCount - values.size();
            long missingUniqueCount = Math.min(missingValueCount, requiredUniqueCount - objectSet.size());

            System.err.println("After histogram processing, still missing " + missingValueCount + " values");
            System.err.println("Generating " + missingUniqueCount + " more unique values as fallback");

            List<Object> fallbackUniqueValues = new ArrayList<>();
            for (long i = 0; i < missingUniqueCount; i++) {
                Object val = generateValue(columnMetadata);
                int attempts = 0;
                while (!objectSet.add(val) && attempts < 10000) {
                    val = generateValue(columnMetadata);
                    attempts++;
                }
                if (attempts < 10000) {
                    fallbackUniqueValues.add(val);
                }
            }

            if (!fallbackUniqueValues.isEmpty()) {
                System.err.println("Filling " + missingValueCount + " missing values from " + fallbackUniqueValues.size() + " fallback unique values");
                for (long i = 0; i < missingValueCount; i++) {
                    values.add(fallbackUniqueValues.get(random.nextInt(fallbackUniqueValues.size())));
                }
            } else {
                System.err.println("Warning: Could not generate fallback values, filling with nulls");
                for (long i = 0; i < missingValueCount; i++) {
                    values.add(null);
                }
            }
        }

        return values;
    }

    private Object generateValue(ColumnMetadata column, Object leftBound, Object rightBound) {
        String dataType = column.getDataType().toLowerCase();

        return switch (dataType) {
            case "smallint", "int2" -> (short)random.nextInt((short)leftBound, (short)rightBound);
            case "integer", "int4", "int" -> random.nextInt((int)leftBound, (int)rightBound);
            case "bigint", "int8" -> random.nextLong((long)leftBound, (long)rightBound);
            case "real", "float4" -> random.nextFloat() * ((float)rightBound - (float)leftBound) + (float)leftBound;
            case "double precision", "float8" -> random.nextDouble() * ((double)rightBound - (double)leftBound) + (double)leftBound;
            case "varchar", "text", "char" -> generateString(column, (String)leftBound, (String)rightBound);
            case "boolean" -> random.nextBoolean();
            case "date" -> generateDate();
            case "timestamp" -> generateTimestamp();
            case "timestamp with time zone", "timestamptz" -> generateTimestampWithTimeZone();
            case "time without time zone", "time" -> generateTimeWithoutTimeZone();
            case "interval" -> generateInterval();
            case "tstzrange" -> generateTstzRange();
            case "point" -> generatePoint();
            case "jsonb", "json" -> generateJson();
            default -> {
                if (dataType.contains("numeric(") || dataType.contains("decimal(")) {
                    yield generateNumeric(dataType);
                }
                yield "value_" + random.nextInt(1000);
            }
        };
    }

    private Object generateValue(ColumnMetadata column) {
        String dataType = column.getDataType().toLowerCase();

        return switch (dataType) {
            case "smallint", "int2" -> (short)random.nextInt(Short.MIN_VALUE, Short.MAX_VALUE);
            case "integer", "int4", "int" -> random.nextInt();
            case "bigint", "int8" -> random.nextLong();
            case "real", "float4" -> random.nextFloat() * 1000;
            case "double precision", "float8" -> random.nextDouble() * 1000;
            case "varchar", "text", "char" -> generateString(column);
            case "boolean" -> random.nextBoolean();
            case "date" -> generateDate();
            case "timestamp" -> generateTimestamp();
            case "timestamp with time zone", "timestamptz" -> generateTimestampWithTimeZone();
            case "time without time zone", "time" -> generateTimeWithoutTimeZone();
            case "interval" -> generateInterval();
            case "tstzrange" -> generateTstzRange();
            case "point" -> generatePoint();
            case "jsonb", "json" -> generateJson();
            default -> {
                if (dataType.contains("numeric(") || dataType.contains("decimal(")) {
                    yield generateNumeric(dataType);
                }
                yield "value_" + random.nextInt(1000);
            }
        };
    }

    /**
     * Парсит типы numeric(precision, scale) или decimal(precision, scale)
     * и генерирует значение с учётом этих параметров.
     *
     * @param dataType строка типа "numeric(10,2)" или "decimal(5,3)"
     * @return сгенерированное число
     */
    private Object generateNumeric(String dataType) {
        Pattern pattern = Pattern.compile("(numeric|decimal)\\((\\d+)(?:,(\\d+))?\\)");
        Matcher matcher = pattern.matcher(dataType);

        if (matcher.find()) {
            int precision = Integer.parseInt(matcher.group(2));
            int scale = matcher.group(3) != null ? Integer.parseInt(matcher.group(3)) : 0;

            int integerDigits = precision - scale;

            double maxValue = Math.pow(10, integerDigits) - 1;

            double value = random.nextDouble() * maxValue;

            double multiplier = Math.pow(10, scale);
            return (Math.round(value * multiplier) / multiplier);
        }

        return (Math.round(random.nextDouble() * 1000 * 100.0) / 100.0);
    }

    private String generateString(ColumnMetadata column, String leftBound, String rightBound) {
        if (leftBound == null || rightBound == null) {
            return generateString(column);
        }

        // Определяем максимальную длину для генерации
        int maxLength = Math.max(leftBound.length(), rightBound.length());
        if (column.getAvgTupleSize() > 0) {
            maxLength = column.getAvgTupleSize();
        }

        StringBuilder result = new StringBuilder();
        int minLen = Math.min(leftBound.length(), rightBound.length());

        // Генерируем строку посимвольно, учитывая лексикографические границы
        for (int i = 0; i < maxLength; i++) {
            char leftChar = i < leftBound.length() ? leftBound.charAt(i) : '\0';
            char rightChar = i < rightBound.length() ? rightBound.charAt(i) : Character.MAX_VALUE;

            // Если мы уже вышли за пределы общего префикса
            if (i >= minLen) {
                // Если мы в зоне leftBound (leftBound длиннее)
                if (i < leftBound.length()) {
                    // Генерируем символ >= leftChar
                    char randomChar = (char) (leftChar + random.nextInt(Character.MAX_VALUE - leftChar + 1));
                    result.append(randomChar);
                }
                // Если мы в зоне rightBound (rightBound длиннее)
                else if (i < rightBound.length()) {
                    // Генерируем символ <= rightChar
                    char randomChar = (char) random.nextInt(rightChar + 1);
                    result.append(randomChar);
                } else {
                    // Можем добавить любой символ
                    result.append((char) (32 + random.nextInt(95))); // Печатные ASCII символы
                }
            } else {
                // В пределах общей длины
                if (leftChar == rightChar) {
                    // Символы совпадают - используем его
                    result.append(leftChar);
                } else if (leftChar < rightChar) {
                    // Генерируем символ между leftChar и rightChar
                    int range = rightChar - leftChar + 1;
                    char randomChar = (char) (leftChar + random.nextInt(range));
                    result.append(randomChar);

                    // После первого различающегося символа можем генерировать свободно
                    if (randomChar > leftChar && randomChar < rightChar) {
                        // Заполняем оставшуюся часть случайными символами
                        int remainingLength = random.nextInt(Math.max(1, maxLength - i));
                        for (int j = 0; j < remainingLength; j++) {
                            result.append((char) (32 + random.nextInt(95)));
                        }
                        break;
                    }
                } else {
                    // leftChar > rightChar - некорректная ситуация, берем средний символ
                    result.append((char) ((leftChar + rightChar) / 2));
                }
            }
        }

        return result.toString();
    }

    private String generateString(ColumnMetadata column) {
        long length = column.getAvgTupleSize() > 0 ? column.getAvgTupleSize() : 10;

        String[] fakerOptions = {
            faker.name().fullName(),
            faker.address().city(),
            faker.company().name(),
            faker.internet().emailAddress(),
            faker.lorem().word(),
            faker.commerce().productName(),
            faker.book().title()
        };

        String result = fakerOptions[random.nextInt(fakerOptions.length)];

        if (result.length() > length) {
            return result.substring(0, (int) length);
        } else if (result.length() < length) {
            StringBuilder sb = new StringBuilder(result);
            String chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ";
            while (sb.length() < length) {
                sb.append(chars.charAt(random.nextInt(chars.length())));
            }
            return sb.toString();
        }

        return result;
    }

    private java.sql.Date generateDate() {
        return new java.sql.Date(faker.date().past(730, java.util.concurrent.TimeUnit.DAYS).getTime());
    }

    private java.sql.Timestamp generateTimestamp() {
        return new java.sql.Timestamp(faker.date().past(730, java.util.concurrent.TimeUnit.DAYS).getTime());
    }

    private String generateTimestampWithTimeZone() {
        java.util.Date date = faker.date().past(730, java.util.concurrent.TimeUnit.DAYS);
        LocalDateTime dateTime = LocalDateTime.ofInstant(date.toInstant(), java.time.ZoneId.systemDefault());
        return dateTime.format(DateTimeFormatter.ISO_LOCAL_DATE_TIME) + "Z";
    }

    private String generateTimeWithoutTimeZone() {
        LocalTime randomTime = LocalTime.of(
                faker.number().numberBetween(0, 24),
                faker.number().numberBetween(0, 60),
                faker.number().numberBetween(0, 60)
        );
        return randomTime.format(DateTimeFormatter.ISO_LOCAL_TIME);
    }

    private String generateInterval() {
        int days = faker.number().numberBetween(0, 30);
        int hours = faker.number().numberBetween(0, 24);
        int minutes = faker.number().numberBetween(0, 60);
        int seconds = faker.number().numberBetween(0, 60);

        List<String> intervals = Arrays.asList(
                days + " days",
                hours + " hours",
                minutes + " minutes",
                seconds + " seconds",
                days + " days " + hours + " hours",
                hours + " hours " + minutes + " minutes",
                "P" + days + "DT" + hours + "H" + minutes + "M" + seconds + "S"
        );

        return intervals.get(faker.number().numberBetween(0, intervals.size()));
    }

    private String generateTstzRange() {
        java.util.Date startDate = faker.date().past(365, java.util.concurrent.TimeUnit.DAYS);
        LocalDateTime start = LocalDateTime.ofInstant(startDate.toInstant(), java.time.ZoneId.systemDefault());
        LocalDateTime end = start.plusDays(faker.number().numberBetween(1, 31));

        String startStr = start.format(DateTimeFormatter.ISO_LOCAL_DATE_TIME) + "Z";
        String endStr = end.format(DateTimeFormatter.ISO_LOCAL_DATE_TIME) + "Z";

        return "[\"" + startStr + "\",\"" + endStr + "\")";
    }

    private PGpoint generatePoint() {
        double x = Math.round(faker.number().randomDouble(6, -90, 90) * 1000000.0) / 1000000.0;
        double y = Math.round(faker.number().randomDouble(6, -180, 180) * 1000000.0) / 1000000.0;
        return new PGpoint(x, y);
    }

    private String generateJson() {
        List<String> jsonTemplates = Arrays.asList(
                "{\"id\": " + faker.number().numberBetween(1, 1000) + ", \"name\": \"" + faker.name().username() + "\", \"active\": " + faker.bool().bool() + "}",
                "{\"coordinates\": {\"x\": " + faker.number().numberBetween(0, 100) + ", \"y\": " + faker.number().numberBetween(0, 100) + "}, \"type\": \"point\"}",
                "{\"tags\": [\"" + faker.lorem().word() + "\", \"" + faker.lorem().word() + "\", \"" + faker.lorem().word() + "\"], \"count\": " + faker.number().numberBetween(0, 50) + "}",
                "{\"metadata\": {\"created\": \"" + LocalDateTime.ofInstant(faker.date().past(30, java.util.concurrent.TimeUnit.DAYS).toInstant(), java.time.ZoneId.systemDefault()) + "\", \"version\": " + faker.number().randomDouble(2, 1, 6) + "}}",
                "{\"settings\": {\"notifications\": " + faker.bool().bool() + ", \"theme\": \"" + (faker.bool().bool() ? "dark" : "light") + "\"}}"
        );

        return jsonTemplates.get(faker.number().numberBetween(0, jsonTemplates.size()));
    }
}