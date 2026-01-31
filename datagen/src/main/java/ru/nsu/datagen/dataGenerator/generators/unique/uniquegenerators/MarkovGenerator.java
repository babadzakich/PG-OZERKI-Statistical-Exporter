package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.math.BigDecimal;
import java.math.RoundingMode;
import java.time.LocalTime;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import com.github.javafaker.Faker;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;

@Slf4j
public class MarkovGenerator implements UniqueKeyGenerator {
    private final List<Map<Object, Double>> columns;
    private final List<String> names;
    private final int ncols;
    private final int recordCount;
    private final List<Integer> recordSize;
    private final List<Double> ndistincts;
    private final List<String> types;
    private final Random random = new Random(System.currentTimeMillis());
    private final Faker faker = new Faker(random);

    public MarkovGenerator(List<ColumnMetadata> columnsMetadata, int recordCount) {
            this.columns = new ArrayList<>();
            this.recordSize = new ArrayList<>();
            this.names = new ArrayList<>();
            this.ndistincts = new ArrayList<>();
            this.types = new ArrayList<>();
            
            for (ColumnMetadata col : columnsMetadata) {
                this.columns.add(col.getMcv());
                this.names.add(col.getName());
                this.recordSize.add(col.getAvgTupleSize());
                this.ndistincts.add(col.getNdistinct());
                this.types.add(col.getDataType());
            }
            
            this.ncols = this.columns.size();
            this.recordCount = recordCount;
        }

    @Override
    public void generate(Map<String, List<Object>> columnData) {
        log.info("Запуск Markov генератора для {} уникальных записей и {} колонок", recordCount, ncols);
        List<List<Object>> uniqueValues = generateUnique(recordCount, 200);
        
        for (int colIdx = 0; colIdx < names.size(); colIdx++) {
            List<Object> columnValues = new ArrayList<>();
            for (List<Object> uniqueValue : uniqueValues) {
                columnValues.add(uniqueValue.get(colIdx));
            }
            columnData.put(names.get(colIdx), columnValues);
        }
    }

    @Override
    public List<Object> generate() {
        throw new UnsupportedOperationException("Markov generator can only be used for Multiple column unique, " +
                "use generate(Map<String, List<Object>> columnData) instead.");
    }
    
    private Object weightedChoice(Map<Object, Double> dist) {
        double r = random.nextDouble();
        double cum = 0.0;
        
        for (Map.Entry<Object, Double> entry : dist.entrySet()) {
            cum += entry.getValue();
            if (r <= cum) {
                return entry.getKey();
            }
        }
        return dist.keySet().iterator().next();
    }
    
    public List<List<Object>> generateUnique(int count, int maxAttemptsPerItem) {
        Set<List<Object>> uniques = new HashSet<>();
        List<List<Object>> results = new ArrayList<>();
        int attempts = 0;
        int maxAttempts = count * maxAttemptsPerItem;
        
        // Фаза 1: Генерация из реальных данных
        if (columns.stream().noneMatch(Map::isEmpty)) {
            while (results.size() < count && attempts < maxAttempts) {
                attempts++;

                List<Object> seq = sampleOneWithUpdate(columns);
                if (uniques.add(seq)) {
                    results.add(seq);
                    decreaseProbabilities(columns, seq);
                }
            }
        }
        
        // Фаза 2: Если не хватило - расширяем пространство синтетическими данными
        if (results.size() < count) {
            log.debug("Недостаточно уникальных комбинаций из реальных данных. Сгенерировано: {}/{}. " +
                            "Расширяем пространство синтетическими значениями...",
                    results.size(), count);

            
            expandColumnsForRequiredSpace(columns, count);
            
            // Сбрасываем вероятности до равномерных после расширения
            // чтобы синтетические значения имели шанс быть выбранными
            for (Map<Object, Double> col : columns) {
                double uniformProb = 1.0 / col.size();
                col.replaceAll((k, v) -> uniformProb);
            }
            
            // Продолжаем генерацию с расширенным пространством
            attempts = 0;
            while (results.size() < count && attempts < maxAttempts) {
                attempts++;
                
                List<Object> seq = sampleOneWithUpdate(columns);
                if (uniques.add(seq)) {
                    results.add(seq);
                    // Не уменьшаем вероятности в фазе 2 для равномерного использования пространства
                    // decreaseProbabilities(columns, seq);
                }
            }
            
            if (results.size() < count) {
                throw new RuntimeException(
                    "Не удалось получить требуемое количество уникальных элементов (" 
                    + count + "). Получено только: " + results.size()
                );
            }
        }

        log.debug("Всего попыток: {}, Уникальных записей: {}", attempts, results.size());
        return results;
    }
    
    /**
     * Генерирует одну последовательность из модифицируемых распределений
     */
    private List<Object> sampleOneWithUpdate(List<Map<Object, Double>> workingCols) {
        List<Object> seq = new ArrayList<>();
        
        Object token = weightedChoice(workingCols.getFirst());
        seq.add(token);
        
        for (int i = 1; i < ncols; i++) {
            token = weightedChoice(workingCols.get(i));
            seq.add(token);
        }
        
        return seq;
    }
    
    /**
     * Уменьшает вероятности использованных значений
     */
    private void decreaseProbabilities(List<Map<Object, Double>> workingCols, List<Object> usedSeq) {
        for (int i = 0; i < usedSeq.size(); i++) {
            Object usedValue = usedSeq.get(i);
            Map<Object, Double> col = workingCols.get(i);
            
            Double currentProb = col.get(usedValue);
            if (currentProb != null && currentProb > 0) {
                double newProb = currentProb * 0.5;
                col.put(usedValue, newProb);
                
                double sum = col.values().stream().mapToDouble(Double::doubleValue).sum();
                if (sum > 0) {
                    col.replaceAll((k, v) -> col.get(k) / sum);
                }
            }
        }
    }
    
    // ========== EXPAND COLUMNS ==========
    
    /**
     * Расширяет рабочие колонки синтетическими значениями, используя ndistinct как ориентир.
     */
    private void expandColumnsForRequiredSpace(List<Map<Object, Double>> columns, int totalRequired) {
        for (int i = 0; i < columns.size(); i++) {
            Map<Object, Double> col = columns.get(i);
            String type = types.get(i);
            double ndistinctVal = ndistincts.get(i);
            
            if (ndistinctVal < 0) {
                ndistinctVal = -ndistinctVal * recordCount;
            }
            

            int targetSize = (int) Math.min(ndistinctVal, totalRequired * 2.0);
            
            int currentSize = col.size();
            int toAdd = targetSize - currentSize;
            
            if (toAdd > 0) {
                int avgTupleSize = recordSize.get(i);
                double avgProb = col.values().stream()
                        .mapToDouble(Double::doubleValue)
                        .average()
                        .orElse(1.0 / (currentSize + toAdd));
                
                int added = 0;
                int attempts = 0;
                
                while (added < toAdd && attempts < toAdd * 100) {
                    attempts++;
                    String candidate = generateRandomString(avgTupleSize, type);
                    if (!col.containsKey(candidate)) {
                        col.put(candidate, avgProb);
                        added++;
                    }
                }
                
                // Нормализуем вероятности
                double sum = col.values().stream().mapToDouble(Double::doubleValue).sum();
                if (sum > 0) {
                    col.replaceAll((k, v) -> col.get(k) / sum);
                }
                log.debug("Добавлено {} синтетических значений в колонку {}", added, names.get(i));
            }
        }
    }
    
    /**
     * Генерирует случайную строку заданной длины из цифр и букв
     */
    private String generateRandomString(int length, String type) {

        if (length <= 0) return "";
//        System.err.println(type);
        switch (type) {
            case "smallint", "smallserial":
                return String.valueOf(random.nextInt(65536) - 32768);
            case "integer", "serial":
                return String.valueOf(random.nextInt());
            case "bigint", "bigserial":
                return String.valueOf(random.nextLong());
            case "real":
                return String.valueOf(random.nextFloat());
            case "double precision":
                return String.valueOf(random.nextDouble());
            case "money":
                return faker.commerce().price(0, 1000000).replace(",", ".");
            case "bytea":
                byte[] bytes = new byte[length];
                random.nextBytes(bytes);
                return "\\x" + java.util.HexFormat.of().formatHex(bytes);
            case "timestamp", "timestamp without time zone":
                return faker.date().past(3650, TimeUnit.DAYS).toInstant().atZone(ZoneId.systemDefault()).toLocalDate().toString();
            case "timestamp with time zone":
                return faker.date().past(365, TimeUnit.DAYS).toInstant().atZone(ZoneId.systemDefault())
                        .withZoneSameInstant(ZoneId.of(ZoneId.getAvailableZoneIds().stream()
                        .skip(random.nextInt(ZoneId.getAvailableZoneIds().size()))
                        .findFirst().orElse("UTC"))).toString();
            case "date":
                return faker.date().past(365, TimeUnit.DAYS).toInstant().atZone(ZoneId.systemDefault()).toLocalDate().toString();
            case "time", "time without time zone", "interval":
                return LocalTime.of(
                        random.nextInt(24),
                        random.nextInt(60),
                        random.nextInt(60)
                ).toString();

            case "boolean":
                return random.nextBoolean() ? "t" : "f";
        }

        StringBuilder sb = new StringBuilder(length);
        if (type.toLowerCase().startsWith("numeric") || type.toLowerCase().startsWith("decimal")) {
            Matcher matcher = Pattern.compile("(\\d+),\\s*(\\d+)")
                    .matcher(type);
            int precision = -1;
            int scale = -1;

            if (matcher.find()) {
                precision = Integer.parseInt(matcher.group(1));
                scale = Integer.parseInt(matcher.group(2));
            } else {
                matcher = Pattern.compile("(\\d+)")
                        .matcher(type);
                if (matcher.find()) {
                    precision = Integer.parseInt(matcher.group(1));
                    scale = 0;
                }
            }

            if (precision > 0 && scale >= 0) {
                int integerDigits = precision - scale;
                if (integerDigits < 0) integerDigits = 0;

                long maxBound = (long) Math.pow(10, integerDigits);
                long minBound = -maxBound;

                double randomDouble = faker.number().randomDouble(scale, minBound, maxBound);

                BigDecimal randomNumeric = BigDecimal.valueOf(randomDouble)
                        .setScale(scale, RoundingMode.HALF_UP);

                return randomNumeric.toString();
            }
        }

        if (type.toLowerCase().contains("char") || type.toLowerCase().contains("text")) {
            String fakeText = faker.lorem().paragraph(1);
            return fakeText.substring(0, Math.min(length, fakeText.length()));
        }

        String chars = "01234563456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        for (int i = 0; i < length; i++) {
            sb.append(chars.charAt(random.nextInt(chars.length())));
        }
        return sb.toString();
    }
}
