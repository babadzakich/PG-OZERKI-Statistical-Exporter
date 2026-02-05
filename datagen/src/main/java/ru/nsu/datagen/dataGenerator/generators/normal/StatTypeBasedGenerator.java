package ru.nsu.datagen.dataGenerator.generators.normal;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.SimpleUniqueGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

import java.math.BigInteger;
import java.security.SecureRandom;
import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.time.format.DateTimeFormatterBuilder;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.postgresql.geometric.PGpoint;
import net.datafaker.Faker;

@Slf4j
public class StatTypeBasedGenerator implements NormalValueGenerator {
    private static final int BASE_INT = 65537; // 65536 (max char) + 1 for shift; terminator = 0
    private static final BigInteger BASE = BigInteger.valueOf(BASE_INT);

    private final Random random = new Random();
    private final SecureRandom secureRandom = new SecureRandom();
    private final Faker faker = new Faker();

    @Override
    public List<Object> generateValues(ColumnMetadata columnMetadata) {
        log.info("Generating values for column: {}", columnMetadata.getName());
        if (columnMetadata.getNdistinct() == -1) {
            log.debug("Using SimpleUniqueGenerator for column: {}, because ndistinct = -1", columnMetadata.getName());
            UniqueKeyGenerator generator = new SimpleUniqueGenerator(List.of(columnMetadata), columnMetadata.getRecordCount());
            return generator.generate();
        }
        Set<Object> objectSet = new HashSet<>();

        long requiredUniqueCount = columnMetadata.getNdistinct() < 0
            ? (long)(Math.abs(columnMetadata.getNdistinct()) * columnMetadata.getRecordCount())
            : (long)columnMetadata.getNdistinct();
        log.debug("Generating {} unique values for column {} where ndistinct = {}", requiredUniqueCount, columnMetadata.getName(), columnMetadata.getNdistinct());
        List<Object> values = new ArrayList<>();
        int recordCount = columnMetadata.getRecordCount();
        log.debug("Total record count for column {}: {}", columnMetadata.getName(), recordCount);
        double nullPercentage = columnMetadata.getNullPercentage();
        log.debug("Null percentage for column {}: {}", columnMetadata.getName(), nullPercentage);

        log.debug("Processing MCVs for column {}", columnMetadata.getName());
        for (Object mvcValue : columnMetadata.getMcv().keySet()) {
            objectSet.add(mvcValue);
            double freq = columnMetadata.getMcv().get(mvcValue);
            long mvcCount = Math.round(freq * recordCount);
            log.trace("Adding MVC value: {} with frequency: {} resulting in count: {}", mvcValue, freq, mvcCount);
            for (long j = 0; j < mvcCount; j++) {
                values.add(mvcValue);
            }
        }

        int nullCount = (int)(recordCount * (nullPercentage / 100.0));
        log.debug("Adding {} null values for column {}", nullCount, columnMetadata.getName());
        for (int i = 0; i < nullCount; i++) {
            values.add(null);
        }

        long mvcCount = objectSet.size();
        long hasNulls = nullPercentage > 0 ? 1 : 0;
        long remainingUniqueCount = requiredUniqueCount - mvcCount - hasNulls;
        long remainingValueCount = recordCount - values.size();
        log.debug("Remaining unique count to generate for column {}: {}", columnMetadata.getName(), remainingUniqueCount);
        log.debug("Remaining value count to fill for column {}: {}", columnMetadata.getName(), remainingValueCount);

        int histogramBuckets = columnMetadata.getHistogramm().size() - 1;

        if (histogramBuckets > 0 && remainingUniqueCount > 0 && remainingValueCount > 0) {
            log.debug("Using histogram-based generation for column {} with {} buckets", columnMetadata.getName(), histogramBuckets);
            for (int i = 0; i < histogramBuckets; i++) {
                Object lowerBound = columnMetadata.getHistogramm().get(i);
                Object upperBound = columnMetadata.getHistogramm().get(i + 1);
                log.trace("Bucket {}: Lower bound = {}, Upper bound = {}", i, lowerBound, upperBound);

                // Распределяем уникальные значения равномерно по бакетам с учётом остатка
                // Например: 2002 значения на 100 бакетов = 20 + (1 если i < 2), т.е. первые 2 бакета по 21, остальные по 20
                long baseUniqueCount = remainingUniqueCount / histogramBuckets;
                long uniqueRemainder = remainingUniqueCount % histogramBuckets;
                long rangeUniqueCount = baseUniqueCount + (i < uniqueRemainder ? 1 : 0);

                // Распределяем общее количество значений равномерно по бакетам с учётом остатка
                long baseValueCount = remainingValueCount / histogramBuckets;
                long valueRemainder = remainingValueCount % histogramBuckets;
                long rangeValueCount = baseValueCount + (i < valueRemainder ? 1 : 0);

                log.trace("Bucket {}: Calculated unique count = {}, value count = {}", i, rangeUniqueCount, rangeValueCount);

                // Генерируем уникальные значения для данного бакета
                List<Object> bucketUniqueValues = new ArrayList<>();
                bucketUniqueValues.add(lowerBound);
                bucketUniqueValues.add(upperBound);
                for (long j = 2; j < rangeUniqueCount; j++) {
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
                log.trace("Bucket {}: Generated {} unique values: {}", i, bucketUniqueValues.size(), bucketUniqueValues);

                // Заполняем values случайными значениями из сгенерированных уникальных
                if (!bucketUniqueValues.isEmpty()) {
                    log.trace("Bucket {}: Filling {} values from unique values", i, rangeValueCount);
                    for (int j = i == 0 ? 0 : 1; j < rangeUniqueCount; j++) {
                        values.add(bucketUniqueValues.get(j));
                    }
                    log.trace("Bucket {}: Added {} unique values, now filling remaining {} values", i, rangeUniqueCount, rangeValueCount - rangeUniqueCount);
                    for (long j = rangeUniqueCount; j < rangeValueCount; j++) {
                        values.add(bucketUniqueValues.get(random.nextInt(bucketUniqueValues.size())));
                    }
                    log.trace("Bucket {}: Filled {} values", i, rangeValueCount);
                } else {
                    log.warn("Warning: Could not generate unique values for bucket {}", i);
                }
            }
        }

        // Fallback: если после обработки гистограммы все еще не хватает значений
        if (values.size() < recordCount) {
            long missingValueCount = recordCount - values.size();
            long missingUniqueCount = Math.min(missingValueCount, requiredUniqueCount - objectSet.size());

            log.debug("Generating fallback values for column {}: missingValueCount = {}, missingUniqueCount = {}", columnMetadata.getName(), missingValueCount, missingUniqueCount);

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
                log.debug("Filling {} missing values from {} fallback unique values for column {}", missingValueCount, fallbackUniqueValues.size(), columnMetadata.getName());
                for (long i = 0; i < missingValueCount; i++) {
                    values.add(fallbackUniqueValues.get(random.nextInt(fallbackUniqueValues.size())));
                }
            } else {
                log.warn("Warning: Could not generate fallback unique values for column {}, filling with nulls", columnMetadata.getName());
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
            case "date" -> generateDateBetween((java.sql.Date)leftBound, (java.sql.Date)rightBound);
            case "timestamp" -> generateTimestampBetween((String)leftBound, (String)rightBound);
            case "timestamp with time zone", "timestamptz" -> generateTimestampWithTimeZoneBetween((String)leftBound, (String)rightBound);
            case "time without time zone", "time" -> generateTimeWithoutTimeZoneBetween((String)leftBound, (String)rightBound);
            case "interval" -> generateIntervalBetween((String)leftBound, (String)rightBound);
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

    /**
     * Кодирует строку в BigInteger (лексикографический порядок сохраняется)
     * digits: s[i] -> (s.charAt(i) & 0xFFFF) + 1, затем добавляем терминатор 0
     */
    private BigInteger encodeString(String s) {
        BigInteger res = BigInteger.ZERO;
        for (int i = 0; i < s.length(); i++) {
            int d = (s.charAt(i) & 0xFFFF) + 1; // 1..65536
            res = res.multiply(BASE).add(BigInteger.valueOf(d));
        }
        // добавляем терминатор
        res = res.multiply(BASE).add(BigInteger.ZERO);
        return res;
    }

    /**
     * Декодирует BigInteger обратно в строку (предполагаем, что в числе есть терминатор)
     */
    private String decodeString(BigInteger encoded) {
        if (encoded.equals(BigInteger.ZERO)) return ""; // пустая строка
        BigInteger cur = encoded;
        // пропускаем завершающий нулевой разряд (терминатор)
        cur = cur.divide(BASE);
        if (cur.equals(BigInteger.ZERO)) return "";
        List<Character> charsReversed = new ArrayList<>();
        while (!cur.equals(BigInteger.ZERO)) {
            BigInteger[] qr = cur.divideAndRemainder(BASE);
            int digit = qr[1].intValue(); // 1..65536
            if (digit == 0) break; // safety
            char ch = (char) (digit - 1);
            charsReversed.add(ch);
            cur = qr[0];
        }
        // собрать в нормальном порядке
        StringBuilder sb = new StringBuilder(charsReversed.size());
        for (int i = charsReversed.size() - 1; i >= 0; i--) {
            sb.append(charsReversed.get(i));
        }
        return sb.toString();
    }

    /**
     * Возвращает случайный BigInteger в диапазоне [0, n-1]
     */
    private BigInteger randomBigIntegerLessThan(BigInteger n) {
        if (n.compareTo(BigInteger.ONE) <= 0) return BigInteger.ZERO;
        BigInteger r;
        int bits = n.bitLength();
        do {
            r = new BigInteger(bits, secureRandom);
        } while (r.compareTo(n) >= 0);
        return r;
    }

    /**
     * Генерирует случайную строку строго между left и right используя лексикографическое кодирование
     */
    private String generateString(ColumnMetadata column, String leftBound, String rightBound) {
        if (leftBound == null || rightBound == null) {
            return generateString(column);
        }

        // если левый > правого, поменяем
        if (leftBound.compareTo(rightBound) >= 0) {
            String tmp = leftBound;
            leftBound = rightBound;
            rightBound = tmp;
        }

        // если границы равны, нет строки между ними
        if (leftBound.equals(rightBound)) {
            return generateString(column);
        }

        BigInteger leftBI = encodeString(leftBound);
        BigInteger rightBI = encodeString(rightBound);

        // проверка пустого промежутка
        BigInteger gap = rightBI.subtract(leftBI).subtract(BigInteger.ONE); // strictly between
        if (gap.compareTo(BigInteger.ZERO) <= 0) {
            log.trace("No string strictly between '{}' and '{}', using fallback generation", leftBound, rightBound);
            return generateString(column);
        }

        BigInteger offset = randomBigIntegerLessThan(gap).add(BigInteger.ONE); // 1..gap
        BigInteger chosen = leftBI.add(offset);
        String result = decodeString(chosen);

        // Если нужно соблюсти avgTupleSize, обрезаем или дополняем
        int targetLength = column.getAvgTupleSize() > 0 ? column.getAvgTupleSize() : result.length();

        if (result.length() > targetLength) {
            result = result.substring(0, targetLength);
        } else if (result.length() < targetLength) {
            // Дополняем случайными символами
            StringBuilder sb = new StringBuilder(result);
            String chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ";
            while (sb.length() < targetLength) {
                sb.append(chars.charAt(random.nextInt(chars.length())));
            }
            result = sb.toString();
        }

        log.trace("Generated string between '{}' and '{}': '{}'", leftBound, rightBound, result);
        return result;
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

    private java.sql.Date generateDateBetween(java.sql.Date leftBound, java.sql.Date rightBound) {
        java.util.Date randomDate = faker.date().between(leftBound, rightBound);
        return new java.sql.Date(randomDate.getTime());
    }

    private java.sql.Timestamp generateTimestamp() {
        return new java.sql.Timestamp(faker.date().past(730, java.util.concurrent.TimeUnit.DAYS).getTime());
    }

    private java.sql.Timestamp generateTimestampBetween(String leftBound, String rightBound) {
        try {
            Date randomDate = getRandomDate(leftBound, rightBound);
            return new java.sql.Timestamp(randomDate.getTime());
        } catch (Exception e) {
            log.warn("Warning: Could not parse timestamp bounds '{}' and '{}', using default generation: {}",
                leftBound, rightBound, e.getMessage());
            return generateTimestamp();
        }
    }

    private String generateTimestampWithTimeZone() {
        java.util.Date date = faker.date().past(730, java.util.concurrent.TimeUnit.DAYS);
        LocalDateTime dateTime = LocalDateTime.ofInstant(date.toInstant(), java.time.ZoneId.systemDefault());
        return dateTime.format(DateTimeFormatter.ISO_LOCAL_DATE_TIME) + "Z";
    }

    private String generateTimestampWithTimeZoneBetween(String leftBound, String rightBound) {
        try {
            Date randomDate = getRandomDate(leftBound, rightBound);
            LocalDateTime randomDateTime = LocalDateTime.ofInstant(randomDate.toInstant(), java.time.ZoneId.systemDefault());

            return randomDateTime.format(DateTimeFormatter.ISO_LOCAL_DATE_TIME) + "Z";
        } catch (Exception e) {
            log.warn("Warning: Could not parse timestamp bounds '{}' and '{}', using default generation: {}",
                leftBound, rightBound, e.getMessage());
            return generateTimestampWithTimeZone();
        }
    }

    private Date getRandomDate(String leftBound, String rightBound) {
        LocalDateTime leftDateTime = parseTimestampWithTimeZone(leftBound);
        LocalDateTime rightDateTime = parseTimestampWithTimeZone(rightBound);

        // Преобразуем в java.util.Date для использования с faker
        Date leftDate = Date.from(leftDateTime.atZone(java.time.ZoneId.systemDefault()).toInstant());
        Date rightDate = Date.from(rightDateTime.atZone(java.time.ZoneId.systemDefault()).toInstant());

        // Используем faker для генерации случайной даты между границами
        return faker.date().between(leftDate, rightDate);
    }

    /**
     * Парсит timestamp with time zone в различных форматах PostgreSQL:
     * - "2026-01-29 03:35:00+07"
     * - "2026-01-29 03:35:00.893332+07"
     * - "2026-01-29 03:35:00+00"
     * - "2026-01-29T03:35:00Z"
     * - "2026-01-29T03:35:00.123456+03:00"
     */
    private LocalDateTime parseTimestampWithTimeZone(String timestamp) {
        log.trace("Parsing timestamp: '{}'", timestamp);

        String normalized = timestamp;

        normalized = normalized.replaceAll("[+-]\\d{2}:\\d{2}$", "");
        log.trace("After removing +HH:MM: '{}'", normalized);

        normalized = normalized.replaceAll("[+-]\\d{2}$", "");
        log.trace("After removing +HH: '{}'", normalized);

        normalized = normalized.replace("Z", "");

        normalized = normalized.replace("T", " ");

        normalized = normalized.trim();

        log.trace("Normalized timestamp: '{}'", normalized);

        DateTimeFormatter formatter = new DateTimeFormatterBuilder()
                .appendPattern("yyyy-MM-dd HH:mm:ss")
                .optionalStart()
                .appendFraction(java.time.temporal.ChronoField.NANO_OF_SECOND, 0, 9, true)
                .optionalEnd()
                .toFormatter();

        return LocalDateTime.parse(normalized, formatter);
    }

    private String generateTimeWithoutTimeZone() {
        LocalTime randomTime = LocalTime.of(
                faker.number().numberBetween(0, 24),
                faker.number().numberBetween(0, 60),
                faker.number().numberBetween(0, 60)
        );
        return randomTime.format(DateTimeFormatter.ISO_LOCAL_TIME);
    }

    private String generateTimeWithoutTimeZoneBetween(String leftBound, String rightBound) {
        try {
            LocalTime leftTime = LocalTime.parse(leftBound, DateTimeFormatter.ISO_LOCAL_TIME);
            LocalTime rightTime = LocalTime.parse(rightBound, DateTimeFormatter.ISO_LOCAL_TIME);

            long leftNano = leftTime.toNanoOfDay();
            long rightNano = rightTime.toNanoOfDay();
            long randomNano = leftNano + (long)(random.nextDouble() * (rightNano - leftNano));

            LocalTime randomTime = LocalTime.ofNanoOfDay(randomNano);
            return randomTime.format(DateTimeFormatter.ISO_LOCAL_TIME);
        } catch (Exception e) {
            log.warn("Warning: Could not parse time bounds, using default generation: {}", e.getMessage());
            return generateTimeWithoutTimeZone();
        }
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

    private String generateIntervalBetween(String leftBound, String rightBound) {
        try {
            long leftSeconds = parseIntervalToSeconds(leftBound);
            long rightSeconds = parseIntervalToSeconds(rightBound);

            long randomSeconds = leftSeconds + faker.number().numberBetween(0L, rightSeconds - leftSeconds + 1);

            return formatSecondsAsInterval(randomSeconds);
        } catch (Exception e) {
            log.warn("Warning: Could not parse interval bounds, using default generation: {}", e.getMessage());
            return generateInterval();
        }
    }

    /**
     * Парсит строку интервала PostgreSQL в количество секунд
     * Поддерживает форматы: "N days", "N hours", "N minutes", "N seconds",
     * "N days N hours", "N hours N minutes", ISO 8601 "PnDTnHnMnS"
     */
    private long parseIntervalToSeconds(String interval) {
        long totalSeconds = 0;

        if (interval.startsWith("P")) {
            Pattern pattern = Pattern.compile("P(?:(\\d+)D)?(?:T(?:(\\d+)H)?(?:(\\d+)M)?(?:(\\d+)S)?)?");
            Matcher matcher = pattern.matcher(interval);
            if (matcher.matches()) {
                if (matcher.group(1) != null) totalSeconds += Long.parseLong(matcher.group(1)) * 86400; // days
                if (matcher.group(2) != null) totalSeconds += Long.parseLong(matcher.group(2)) * 3600;  // hours
                if (matcher.group(3) != null) totalSeconds += Long.parseLong(matcher.group(3)) * 60;    // minutes
                if (matcher.group(4) != null) totalSeconds += Long.parseLong(matcher.group(4));         // seconds
                return totalSeconds;
            }
        }

        Pattern daysPattern = Pattern.compile("(\\d+)\\s+days?");
        Pattern hoursPattern = Pattern.compile("(\\d+)\\s+hours?");
        Pattern minutesPattern = Pattern.compile("(\\d+)\\s+minutes?");
        Pattern secondsPattern = Pattern.compile("(\\d+)\\s+seconds?");

        Matcher matcher = daysPattern.matcher(interval);
        if (matcher.find()) {
            totalSeconds += Long.parseLong(matcher.group(1)) * 86400;
        }

        matcher = hoursPattern.matcher(interval);
        if (matcher.find()) {
            totalSeconds += Long.parseLong(matcher.group(1)) * 3600;
        }

        matcher = minutesPattern.matcher(interval);
        if (matcher.find()) {
            totalSeconds += Long.parseLong(matcher.group(1)) * 60;
        }

        matcher = secondsPattern.matcher(interval);
        if (matcher.find()) {
            totalSeconds += Long.parseLong(matcher.group(1));
        }

        return totalSeconds;
    }

    /**
     * Форматирует количество секунд в строку интервала PostgreSQL
     */
    private String formatSecondsAsInterval(long totalSeconds) {
        long days = totalSeconds / 86400;
        long hours = (totalSeconds % 86400) / 3600;
        long minutes = (totalSeconds % 3600) / 60;
        long seconds = totalSeconds % 60;

        StringBuilder result = new StringBuilder();
        if (days > 0) {
            result.append(days).append(" days");
        }
        if (hours > 0) {
            if (!result.isEmpty()) result.append(" ");
            result.append(hours).append(" hours");
        }
        if (minutes > 0) {
            if (!result.isEmpty()) result.append(" ");
            result.append(minutes).append(" minutes");
        }
        if (seconds > 0 || result.isEmpty()) {
            if (!result.isEmpty()) result.append(" ");
            result.append(seconds).append(" seconds");
        }

        return result.toString();
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

