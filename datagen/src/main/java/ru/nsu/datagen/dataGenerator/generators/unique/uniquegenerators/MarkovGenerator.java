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
    private int ncols;
    private Random random;

    public MarkovGenerator(List<ColumnMetadata> columns) {
            // Выравниваем столбцы
            this.columns = padColumnsToEqualLength(
                columns, 
                "__NULL__"
            );
            
            this.ncols = this.columns.size();
            
            this.transitions = buildDefaultTransitionsFromColumns();
            
            this.random = new Random();
        }

    @Override
    public void generate(Map<String, List<Object>> columnData) {
        // Implementation here
    }

    @Override
    public void generate() {
        // Implementation here
    }

    private static List<Map<String, Double>> padColumnsToEqualLength(
                List<ColumnMetadata> columns,
                String nullPrefix
        ) {
            if (columns.isEmpty()) throw new IllegalArgumentException("Columns list cannot be empty");
            
            int targetLen = columns.stream().mapToInt(col -> col.getMvc().size()).max().orElse(0);
            
            List<Map<String, Double>> padded = new ArrayList<>();
            
            for (int colIdx = 0; colIdx < columns.size(); colIdx++) {
                Map<String, Double> col = new HashMap<>(columns.get(colIdx).getMvc());
                int deficit = targetLen - col.size();
                int k = 1;
                
                while (deficit > 0) {
                    String candidate = nullPrefix + colIdx + "_" + k;
                    if (!col.containsKey(candidate)) {
                        double assigned = col.values().stream()
                                .filter(v -> v > 0)
                                .min(Double::compare)
                                .orElse(1.0);
                        col.put(candidate, assigned);
                        deficit--;
                    }
                    k++;
                }
                padded.add(col);
            }
            
            return padded;
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
    
    // ========== POSSIBLE SPACE SIZE ==========
    
    public long possibleSpaceSize() {
        long size = 1;
        for (Map<String, Double> col : columns) {
            size *= col.size();
        }
        return size;
    }
    
    // ========== GENERATE UNIQUE ==========
    
    public List<List<String>> generateUnique(int count, int maxAttemptsPerItem) {
        long maxSpace = possibleSpaceSize();
        if (count > maxSpace) {
            throw new IllegalArgumentException(
                String.format("Запрошено %d уникальных элементов, а всего возможно только %d", 
                    count, maxSpace)
            );
        }
        
        Set<List<String>> uniques = new HashSet<>();
        List<List<String>> results = new ArrayList<>();
        int attempts = 0;
        
        while (results.size() < count) {
            attempts++;
            if (attempts > count * maxAttemptsPerItem) {
                throw new RuntimeException(
                    "Не удалось получить требуемое количество уникальных элементов методом случайной генерации. " +
                    "Попробуйте увеличить maxAttemptsPerItem или используйте детерминированную выборку."
                );
            }
            
            List<String> seq = sampleOne();
            if (!uniques.contains(seq)) {
                uniques.add(seq);
                results.add(seq);
            }
        }
        
        return results;
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
