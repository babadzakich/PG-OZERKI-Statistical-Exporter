package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

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
    private List<Map<String, Double>> columns;
    private List<Map<String, Map<String, Double>>> transitions;
    private List<String> names;
    private int ncols;
    private int recordCount;
    private List<Integer> recordSize;
    private Random random = new Random(System.currentTimeMillis());

    public MarkovGenerator(List<ColumnMetadata> columnsMetadata, int recordCount) {
            // Используем только реальные данные из метаданных
            this.columns = new ArrayList<>();
            this.recordSize = new ArrayList<>();
            this.names = new ArrayList<>();
            
            for (ColumnMetadata col : columnsMetadata) {
                this.columns.add(new HashMap<>(col.getMvc()));
                this.names.add(col.getName());
                this.recordSize.add(col.getAvgTupleSize());
            }
            
            this.ncols = this.columns.size();
            this.recordCount = recordCount;
            
            // Строим переходы
            this.transitions = buildDefaultTransitionsFromColumns();
        }

    @Override
    public void generate(Map<String, List<Object>> columnData) {
        List<List<String>> uniqueValues = generateUnique(recordCount);
        
        // Транспонируем данные: из списка строк в списки по столбцам
        for (int colIdx = 0; colIdx < names.size(); colIdx++) {
            List<Object> columnValues = new ArrayList<>();
            for (int rowIdx = 0; rowIdx < uniqueValues.size(); rowIdx++) {
                columnValues.add(uniqueValues.get(rowIdx).get(colIdx));
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
    
    // ========== SAMPLE ONE ==========
    
    public List<String> sampleOne() {
        List<String> seq = new ArrayList<>();
        
        // Первый столбец
        String token = weightedChoice(columns.get(0));
        seq.add(token);
        
        // Остальные столбцы
        for (int i = 1; i < ncols; i++) {
            Map<String, Map<String, Double>> trans = null;
            if (i - 1 < transitions.size()) {
                trans = transitions.get(i - 1);
            }
            
            if (trans != null && trans.containsKey(token)) {
                token = weightedChoice(trans.get(token));
            } else {
                token = weightedChoice(columns.get(i));
            }
            seq.add(token);
        }
        
        return seq;
    }
    
    // ========== GENERATE UNIQUE ==========
    
    public List<List<String>> generateUnique(int count, int maxAttemptsPerItem) {
        // Копируем распределения для модификации
        List<Map<String, Double>> workingColumns = new ArrayList<>();
        for (Map<String, Double> col : columns) {
            workingColumns.add(new HashMap<>(col));
        }
        
        Set<List<String>> uniques = new HashSet<>();
        List<List<String>> results = new ArrayList<>();
        int attempts = 0;
        int maxAttempts = count * maxAttemptsPerItem;
        
        // Фаза 1: Генерация из реальных данных
        while (results.size() < count && attempts < maxAttempts) {
            attempts++;
            
            List<String> seq = sampleOneWithUpdate(workingColumns);
            if (!uniques.contains(seq)) {
                uniques.add(seq);
                results.add(seq);
                decreaseProbabilities(workingColumns, seq);
            }
        }
        
        // Фаза 2: Если не хватило - расширяем пространство синтетическими данными
        if (results.size() < count) {
            System.err.println("Недостаточно уникальных комбинаций из реальных данных. "
                    + "Сгенерировано: " + results.size() + "/" + count
                    + ". Расширяем пространство синтетическими значениями...");
            
            int needed = count - results.size();
            expandColumnsForRequiredSpace(workingColumns, needed);
            
            // Продолжаем генерацию с расширенным пространством
            attempts = 0;
            while (results.size() < count && attempts < maxAttempts) {
                attempts++;
                
                List<String> seq = sampleOneWithUpdate(workingColumns);
                if (!uniques.contains(seq)) {
                    uniques.add(seq);
                    results.add(seq);
                    decreaseProbabilities(workingColumns, seq);
                }
            }
            
            if (results.size() < count) {
                throw new RuntimeException(
                    "Не удалось получить требуемое количество уникальных элементов (" 
                    + count + "). Получено только: " + results.size()
                );
            }
        }
        
        System.err.println("Успешно сгенерировано " + results.size() + " уникальных записей");
        return results;
    }
    
    /**
     * Генерирует одну последовательность из модифицируемых распределений
     */
    private List<String> sampleOneWithUpdate(List<Map<String, Double>> workingCols) {
        List<String> seq = new ArrayList<>();
        
        // Первый столбец
        String token = weightedChoice(workingCols.get(0));
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
                    for (String key : col.keySet()) {
                        col.put(key, col.get(key) / sum);
                    }
                }
            }
        }
    }
    
    // ========== EXPAND COLUMNS ==========
    
    /**
     * Расширяет рабочие колонки синтетическими значениями, сохраняя распределение.
     * Добавляет минимально необходимое количество значений для генерации нужного числа уникальных комбинаций.
     */
    private void expandColumnsForRequiredSpace(List<Map<String, Double>> workingColumns, int neededCombinations) {
        // Определяем, в какую колонку добавить значения (выбираем самую маленькую)
        int minColIdx = 0;
        int minSize = Integer.MAX_VALUE;
        
        for (int i = 0; i < workingColumns.size(); i++) {
            int size = workingColumns.get(i).size();
            if (size < minSize) {
                minSize = size;
                minColIdx = i;
            }
        }
        
        // Рассчитываем сколько значений нужно добавить
        // Добавляем с запасом чтобы наверняка хватило уникальных комбинаций
        long currentSpace = possibleSpaceSize();
        int toAdd = (int)Math.ceil((double)neededCombinations * 1.5 / currentSpace * minSize);
        if (toAdd < 1) toAdd = Math.min(10, neededCombinations);
        
        Map<String, Double> targetCol = workingColumns.get(minColIdx);
        int avgTupleSize = recordSize.get(minColIdx);
        
        // Вычисляем среднюю вероятность для новых значений
        double avgProb = targetCol.values().stream()
                .mapToDouble(Double::doubleValue)
                .average()
                .orElse(1.0 / (targetCol.size() + toAdd));
        
        // Добавляем синтетические значения
        int added = 0;
        int attempts = 0;
        while (added < toAdd && attempts < toAdd * 100) {
            attempts++;
            String candidate = generateRandomString(avgTupleSize);
            if (!targetCol.containsKey(candidate)) {
                targetCol.put(candidate, avgProb);
                added++;
            }
        }
        
        // Нормализуем вероятности чтобы сумма = 1
        double sum = targetCol.values().stream().mapToDouble(Double::doubleValue).sum();
        if (sum > 0) {
            for (String key : targetCol.keySet()) {
                targetCol.put(key, targetCol.get(key) / sum);
            }
        }
        
        System.err.println("Добавлено " + added + " синтетических значений в колонку " + names.get(minColIdx));
    }
    
    /**
     * Генерирует случайную строку заданной длины из цифр и букв
     */
    private String generateRandomString(int length) {
        if (length <= 0) return "";
        
        String chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        StringBuilder sb = new StringBuilder(length);
        
        for (int i = 0; i < length; i++) {
            sb.append(chars.charAt(random.nextInt(chars.length())));
        }
        
        return sb.toString();
    }
    
    public List<List<String>> generateUnique(int count) {
        return generateUnique(count, 200);
    }
    
    // ========== BUILD TRANSITIONS FROM ROWS ==========
    
    public static List<Map<String, Map<String, Integer>>> buildTransitionsFromRows(
            List<List<String>> rows
    ) {
        if (rows.isEmpty()) return new ArrayList<>();
        
        int ncols = rows.get(0).size();
        List<Map<String, Map<String, Integer>>> transitions = new ArrayList<>();
        
        for (int i = 0; i < ncols - 1; i++) {
            transitions.add(new HashMap<>());
        }
        
        for (List<String> row : rows) {
            if (row.size() != ncols) {
                throw new IllegalArgumentException("Все строки должны иметь одинаковую длину");
            }
            
            for (int i = 0; i < ncols - 1; i++) {
                String from = row.get(i);
                String to = row.get(i + 1);
                
                transitions.get(i)
                    .computeIfAbsent(from, k -> new HashMap<>())
                    .merge(to, 1, Integer::sum);
            }
        }
        
        // Конвертируем Integer в Double для normalize
        List<Map<String, Map<String, Double>>> result = new ArrayList<>();
        for (Map<String, Map<String, Integer>> layer : transitions) {
            Map<String, Map<String, Double>> doubleLayer = new HashMap<>();
            for (Map.Entry<String, Map<String, Integer>> entry : layer.entrySet()) {
                Map<String, Double> doubleMap = entry.getValue().entrySet().stream()
                    .collect(Collectors.toMap(
                        Map.Entry::getKey,
                        e -> e.getValue().doubleValue()
                    ));
                doubleLayer.put(entry.getKey(), doubleMap);
            }
            result.add(doubleLayer);
        }
        
        return transitions;
    }
    
    // ========== ENUMERATE ALL WEIGHTED ==========
    
    public List<Pair<List<String>, Double>> enumerateAllWeighted() {
        List<List<String>> allTokens = columns.stream()
            .map(col -> new ArrayList<>(col.keySet()))
            .collect(Collectors.toList());
        
        List<Pair<List<String>, Double>> combos = new ArrayList<>();
        
        // Генерируем декартово произведение
        generateCartesianProduct(allTokens, 0, new ArrayList<>(), combos);
        
        return combos;
    }
    
    private void generateCartesianProduct(
            List<List<String>> allTokens,
            int depth,
            List<String> current,
            List<Pair<List<String>, Double>> result
    ) {
        if (depth == allTokens.size()) {
            // Вычисляем вес
            double weight = 1.0;
            for (int i = 0; i < current.size(); i++) {
                weight *= columns.get(i).get(current.get(i));
            }
            result.add(new Pair<>(new ArrayList<>(current), weight));
            return;
        }
        
        for (String token : allTokens.get(depth)) {
            current.add(token);
            generateCartesianProduct(allTokens, depth + 1, current, result);
            current.remove(current.size() - 1);
        }
    }
    
    // ========== ENUMERATE ALL MARKOV WEIGHTED ==========
    
    public List<Pair<List<String>, Double>> enumerateAllMarkovWeighted() {
        List<Pair<List<String>, Double>> frontier = new ArrayList<>();
        
        // Инициализация первого столбца
        for (Map.Entry<String, Double> entry : columns.get(0).entrySet()) {
            List<String> seq = new ArrayList<>();
            seq.add(entry.getKey());
            frontier.add(new Pair<>(seq, entry.getValue()));
        }
        
        // Расширяем на остальные столбцы
        for (int i = 1; i < ncols; i++) {
            List<Pair<List<String>, Double>> nextFrontier = new ArrayList<>();
            
            Map<String, Map<String, Double>> trans = null;
            if (i - 1 < transitions.size()) {
                trans = transitions.get(i - 1);
            }
            
            for (Pair<List<String>, Double> pair : frontier) {
                List<String> seq = pair.first;
                double weight = pair.second;
                String prev = seq.get(seq.size() - 1);
                
                Map<String, Double> dist;
                if (trans != null && trans.containsKey(prev)) {
                    dist = trans.get(prev);
                } else {
                    dist = columns.get(i);
                }
                
                for (Map.Entry<String, Double> entry : dist.entrySet()) {
                    if (entry.getValue() > 0) {
                        List<String> newSeq = new ArrayList<>(seq);
                        newSeq.add(entry.getKey());
                        double newWeight = weight * entry.getValue();
                        nextFrontier.add(new Pair<>(newSeq, newWeight));
                    }
                }
            }
            
            frontier = nextFrontier;
        }
        
        return frontier;
    }
    
    // ========== POSSIBLE SPACE SIZE REACHABLE ==========
    
    public int possibleSpaceSizeReachable() {
        return enumerateAllMarkovWeighted().size();
    }
    
    // ========== POSSIBLE SPACE SIZE (ALL COMBINATIONS) ==========
    
    /**
     * Возвращает общее количество возможных комбинаций при независимых столбцах
     * (произведение размеров всех столбцов). Если произведение превышает Long.MAX_VALUE,
     * возвращается Long.MAX_VALUE.
     */
    public long possibleSpaceSize() {
        long product = 1L;
        for (Map<String, Double> col : columns) {
            int sz = col.size();
            if (sz <= 0) return 0L;
            if (product > Long.MAX_VALUE / sz) {
                return Long.MAX_VALUE;
            }
            product *= sz;
        }
        return product;
    }
    
    // ========== WEIGHTED SAMPLE WITHOUT REPLACEMENT MARKOV ==========
    
    public List<List<String>> weightedSampleWithoutReplacementMarkov(int k) {
        List<Pair<List<String>, Double>> combos = enumerateAllMarkovWeighted();
        if (combos.isEmpty()) return new ArrayList<>();
        
        double totalWeight = combos.stream()
            .mapToDouble(p -> p.second)
            .sum();
        
        if (totalWeight <= 0) {
            throw new IllegalArgumentException("Сумма весов достижимых комбинаций равна нулю");
        }
        
        // Нормализуем вероятности
        List<Pair<List<String>, Double>> available = new ArrayList<>();
        for (Pair<List<String>, Double> combo : combos) {
            available.add(new Pair<>(combo.first, combo.second / totalWeight));
        }
        
        List<List<String>> chosen = new ArrayList<>();
        
        for (int i = 0; i < Math.min(k, available.size()); i++) {
            double sum = available.stream().mapToDouble(p -> p.second).sum();
            double r = random.nextDouble() * sum;
            double cum = 0.0;
            
            for (int idx = 0; idx < available.size(); idx++) {
                cum += available.get(idx).second;
                if (r <= cum) {
                    chosen.add(available.get(idx).first);
                    available.remove(idx);
                    break;
                }
            }
        }
        
        return chosen;
    }
        
        // ========== GENERATE UNIQUE SAFELY ==========
        
    public List<List<String>> generateUniqueSafely(int count) {
        int reachable = possibleSpaceSizeReachable();
        if (count > reachable) {
            throw new RuntimeException(
                String.format("Запрошено %d уникальных элементов, но достижимо только %d при данных переходах",
                    count, reachable)
            );
        }
        return weightedSampleWithoutReplacementMarkov(count);
    }
        
        // ========== HELPER CLASS: PAIR ==========
        
    public static class Pair<F, S> {
        public final F first;
        public final S second;
        
        public Pair(F first, S second) {
            this.first = first;
            this.second = second;
        }
        
        @Override
        public String toString() {
            return "(" + first + ", " + second + ")";
        }
    }
}
