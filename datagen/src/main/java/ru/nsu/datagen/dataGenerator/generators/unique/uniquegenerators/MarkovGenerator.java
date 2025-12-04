package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.stream.Collectors;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;


public class MarkovGenerator implements UniqueKeyGenerator {
    private final List<Map<String, Double>> columns;
    private List<Map<String, Map<String, Double>>> transitions;
    private List<String> names;
    private final int ncols;
    private final int recordCount;
    private List<Integer> recordSize;
    private List<Double> ndistincts;
    private Random random = new Random(System.currentTimeMillis());

    public MarkovGenerator(List<ColumnMetadata> columnsMetadata, int recordCount) {
            this.columns = new ArrayList<>();
            this.recordSize = new ArrayList<>();
            this.names = new ArrayList<>();
            this.ndistincts = new ArrayList<>();
            
            for (ColumnMetadata col : columnsMetadata) {
                this.columns.add(new HashMap<>(col.getMvc()));
                this.names.add(col.getName());
                this.recordSize.add(col.getAvgTupleSize());
                this.ndistincts.add(col.getNdistinct());
            }
            
            this.ncols = this.columns.size();
            this.recordCount = recordCount;

            // Строим переходы
            this.transitions = buildDefaultTransitionsFromColumns();
        }

    @Override
    public void generate(Map<String, List<Object>> columnData) {
        List<List<String>> uniqueValues = generateUnique(recordCount, 200);
        
        for (int colIdx = 0; colIdx < names.size(); colIdx++) {
            List<Object> columnValues = new ArrayList<>();
            for (List<String> uniqueValue : uniqueValues) {
                columnValues.add(uniqueValue.get(colIdx));
            }
            columnData.put(names.get(colIdx), columnValues);
        }
    }

    @Override
    public void generate() {
        // Implementation here
    }

    // ========== BUILD DEFAULT TRANSITIONS ==========
    
    private List<Map<String, Map<String, Double>>> buildDefaultTransitionsFromColumns() {
        List<Map<String, Map<String, Double>>> trans = new ArrayList<>();
        
        for (int i = 0; i < ncols - 1; i++) {
            Map<String, Double> nextMarginal = columns.get(i + 1);
            Map<String, Map<String, Double>> layer = new HashMap<>();
            
            for (String prevToken : columns.get(i).keySet()) {
                layer.put(prevToken, new HashMap<>(nextMarginal));
            }
            trans.add(layer);
        }
        
        return trans;
    }
    
    // ========== WEIGHTED CHOICE ==========
    
    private String weightedChoice(Map<String, Double> dist) {
        double r = random.nextDouble();
        double cum = 0.0;
        
        for (Map.Entry<String, Double> entry : dist.entrySet()) {
            cum += entry.getValue();
            if (r <= cum) {
                return entry.getKey();
            }
        }
        
        // Fallback для округления float
        return dist.keySet().iterator().next();
    }
    
    // ========== GENERATE UNIQUE ==========
    
    public List<List<String>> generateUnique(int count, int maxAttemptsPerItem) {
        Set<List<String>> uniques = new HashSet<>();
//        Set<String> uniques = new HashSet<>();
        List<List<String>> results = new ArrayList<>();
        int attempts = 0;
        int maxAttempts = count * maxAttemptsPerItem;
        
        // Фаза 1: Генерация из реальных данных
        while (results.size() < count && attempts < maxAttempts) {
            attempts++;
            
            List<String> seq = sampleOneWithUpdate(columns);
            String checkSeq = String.join(",", seq);
            if (uniques.add(seq)) {
                results.add(seq);
                decreaseProbabilities(columns, seq);
            }
        }
        
        // Фаза 2: Если не хватило - расширяем пространство синтетическими данными
        if (results.size() < count) {
            System.err.println("Недостаточно уникальных комбинаций из реальных данных. "
                    + "Сгенерировано: " + results.size() + "/" + count
                    + ". Расширяем пространство синтетическими значениями...");
            
            expandColumnsForRequiredSpace(columns, count);
            
            // Сбрасываем вероятности до равномерных после расширения
            // чтобы синтетические значения имели шанс быть выбранными
            for (Map<String, Double> col : columns) {
                double uniformProb = 1.0 / col.size();
                col.replaceAll((k, v) -> uniformProb);
            }
            
            // Продолжаем генерацию с расширенным пространством
            attempts = 0;
            while (results.size() < count && attempts < maxAttempts) {
                attempts++;
                
                List<String> seq = sampleOneWithUpdate(columns);
                String checkSeq = String.join(",", seq);
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
        System.err.println(uniques.size());
        System.err.println("Успешно сгенерировано " + results.size() + " уникальных записей");
        try (FileWriter fw = new FileWriter("markov.txt")) {
            for (List<String> seq : results) {
                fw.write(String.join(",", seq) + "\n");
            }
        } catch (IOException e) {
            System.err.println("Error logging: " + e.getMessage());
        }
        return results;
    }
    
    /**
     * Генерирует одну последовательность из модифицируемых распределений
     */
    private List<String> sampleOneWithUpdate(List<Map<String, Double>> workingCols) {
        List<String> seq = new ArrayList<>();
        
        // Первый столбец
        String token = weightedChoice(workingCols.getFirst());
        seq.add(token);
        
        // Остальные столбцы
        for (int i = 1; i < ncols; i++) {
            // Используем маргинальное распределение
            token = weightedChoice(workingCols.get(i));
            seq.add(token);
        }
        
        return seq;
    }
    
    /**
     * Уменьшает вероятности использованных значений
     */
    private void decreaseProbabilities(List<Map<String, Double>> workingCols, List<String> usedSeq) {
        for (int i = 0; i < usedSeq.size(); i++) {
            String usedValue = usedSeq.get(i);
            Map<String, Double> col = workingCols.get(i);
            
            // Уменьшаем вероятность использованного значения
            Double currentProb = col.get(usedValue);
            if (currentProb != null && currentProb > 0) {
                // Уменьшаем на 50% от текущего значения
                double newProb = currentProb * 0.5;
                col.put(usedValue, newProb);
                
                // Перенормализуем распределение
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
    private void expandColumnsForRequiredSpace(List<Map<String, Double>> columns, int totalRequired) {
        for (int i = 0; i < columns.size(); i++) {
            Map<String, Double> col = columns.get(i);
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
                    String candidate = generateRandomString(avgTupleSize);
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
                
                System.err.println("Добавлено " + added + " синтетических значений в колонку " + names.get(i));
            }
        }
    }
    
    /**
     * Генерирует случайную строку заданной длины из цифр и букв
     */
    private String generateRandomString(int length) {
        if (length <= 0) return "";
        
        // String chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        String chars = "0123456789";
        StringBuilder sb = new StringBuilder(length);
        
        for (int i = 0; i < length; i++) {
            sb.append(chars.charAt(random.nextInt(chars.length())));
        }
        
        return sb.toString();
    }
}
