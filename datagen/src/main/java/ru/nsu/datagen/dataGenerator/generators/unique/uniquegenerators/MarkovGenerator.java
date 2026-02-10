package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;

@Slf4j
public class MarkovGenerator implements UniqueKeyGenerator {
    private final List<ColumnMetadata> columnsMetadata;
    private final List<Map<Object, Double>> columns;
    private final List<String> names;
    private final int recordCount;
    private final List<Double> ndistincts;
    private final Random random = new Random(System.currentTimeMillis());

    public MarkovGenerator(List<ColumnMetadata> columnsMetadata, int recordCount) {
            this.columnsMetadata = columnsMetadata;
            this.columns = new ArrayList<>();
            this.names = new ArrayList<>();
            this.ndistincts = new ArrayList<>();

            for (ColumnMetadata col : columnsMetadata) {
                this.columns.add(col.getMcv());
                this.names.add(col.getName());
                this.ndistincts.add(col.getNdistinct());
            }
            
            this.recordCount = recordCount;
        }

    @Override
    public void generate(Map<String, List<Object>> columnData) {
        log.info("Запуск Markov генератора для {} уникальных записей и колонок {}", recordCount, names);
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
        
        for (int i = 1; i < columns.size(); i++) {
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
            double ndistinctVal = ndistincts.get(i);
            ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(columnsMetadata.get(i));
            
            if (ndistinctVal < 0) {
                ndistinctVal = -ndistinctVal * recordCount;
            }

            long targetSize = (long) Math.min(ndistinctVal, totalRequired * 2.0);
            
            int currentSize = col.size();
            long toAdd = targetSize - currentSize;
            
            if (toAdd > 0) {
                double avgProb = col.values().stream()
                        .mapToDouble(Double::doubleValue)
                        .average()
                        .orElse(1.0 / (currentSize + toAdd));
                
                int added = 0;
                int attempts = 0;
                
                while (added < toAdd && attempts < toAdd * 100) {
                    attempts++;
                    Object candidate = generator.generateValue();
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
}
