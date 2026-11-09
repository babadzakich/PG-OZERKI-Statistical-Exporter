package ru.nsu.datagen.importer.explainanalyze;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.model.AnalyzePlanInfo;
import ru.nsu.datagen.dataGenerator.model.PredicateObservation;
import ru.nsu.datagen.importer.explainanalyze.filter.FilterClause;
import ru.nsu.datagen.importer.explainanalyze.filter.FilterExpression;
import ru.nsu.datagen.importer.explainanalyze.filter.FilterParser;

import java.io.File;
import java.io.IOException;
import java.util.*;

@Slf4j
public class ExplainAnalyzeImporter {
    public static Map<String, List<AnalyzePlanInfo>> importExplainAnalyze(String filePath) {
        Map<String, List<AnalyzePlanInfo>> result;
        try {
            ObjectMapper mapper = new ObjectMapper();
            JsonNode rootNode = mapper.readTree(new File(filePath));
            JsonNode explain = rootNode.isArray() ? rootNode.path(0) : rootNode;
            JsonNode planRoot = explain.path("Plan");
            if (!planRoot.isObject()) throw new IllegalArgumentException("EXPLAIN JSON does not contain a Plan object");
            result = traverse(planRoot);
        } catch (IOException e){
            log.error("Error during reading JSON tree: {}", e.getMessage());
            throw new IllegalArgumentException("Cannot read EXPLAIN JSON: " + e.getMessage(), e);
        }
        return result;
    }

    private static Map<String, List<AnalyzePlanInfo>> traverse(JsonNode node) {
        Map<String, List<AnalyzePlanInfo>> result = new HashMap<>();
        
        traverseHelper(node, result);
        return result;
    }

    private static void traverseHelper(JsonNode node, Map<String, List<AnalyzePlanInfo>> result) {
        if (node == null) return;

        handleScanNode(node).ifPresent(info ->
                result.computeIfAbsent(info.getTableName(), k -> new ArrayList<>()).add(info)
        );

        JsonNode plans = node.path("Plans");
        if (plans.isArray()) {
            for (JsonNode child : plans) {
                traverseHelper(child, result);
            }
        }
    }

    private static Optional<AnalyzePlanInfo> handleScanNode(JsonNode node) {
        String nodeType = node.path("Node Type").asText("");

        return switch (nodeType) {
            case "Seq Scan"                    -> handleSeqScan(node);
            case "Index Scan", "Index Only Scan" -> handleIndexScan(node);
            case "Bitmap Heap Scan"            -> handleBitmapScan(node);
            default                            -> Optional.empty();
        };
    }

    private static Optional<AnalyzePlanInfo> handleSeqScan(JsonNode node) {
        if (!node.has("Relation Name")) return Optional.empty();

        long loops = loops(node);
        long actual = rows(node);
        long removed = rowsRemoved(node, "Rows Removed by Filter");
        long total = (actual + removed) * loops;

        return Optional.of(AnalyzePlanInfo.builder()
                .tableName(node.path("Relation Name").asText())
                .scanType(ScanType.SEQ_SCAN)
                .actualRows(actual * loops)
                .totalRows(total)
                .selectivity(total > 0 ? (double) actual / total : 1.0)
                .rawFilter(node.path("Filter").asText(null))
                .predicateObservations(observations(node.path("Filter"), actual * loops, total))
                .build());
    }

    private static Optional<AnalyzePlanInfo> handleIndexScan(JsonNode node) {
        if (!node.has("Relation Name")) return Optional.empty();

        long actual  = rows(node);
        long loops   = loops(node);
        long removed = rowsRemoved(node, "Rows Removed by Filter");

        // total = реальный объём таблицы через этот индекс
        // ndistinct = loops когда actual=1 за итерацию (каждый loop — уникальный lookup)
        long total      = (actual + removed) * loops;
        long ndistinct  = actual == 1 ? loops : -1;  // -1 = неизвестно

        return Optional.of(AnalyzePlanInfo.builder()
                .tableName(node.path("Relation Name").asText())
                .scanType(ScanType.INDEX_SCAN)
                .actualRows(actual)
                .totalRows(total)
                .ndistinct(ndistinct)
                .selectivity(total > 0 ? (double) actual / total : 1.0)
                .rawFilter(node.path("Filter").asText(null))
                .indexCond(node.path("Index Cond").asText(null))
                .predicateObservations(observations(node.path("Filter"), actual * loops, total))
                .build());
    }

    private static Optional<AnalyzePlanInfo> handleBitmapScan(JsonNode node) {
        if (!node.has("Relation Name")) return Optional.empty();

        long actual  = rows(node);
        long loops   = loops(node);
        long removed = node.path("Rows Removed by Filter").asLong(0);
        long total   = (actual + removed) * loops;

        return Optional.of(AnalyzePlanInfo.builder()
                .tableName(node.path("Relation Name").asText())
                .scanType(ScanType.BITMAP_SCAN)
                .actualRows(actual)
                .totalRows(total)
                .selectivity(total > 0 ? (double) actual / total : 1.0)
                .rawFilter(node.path("Filter").asText(null))
                .indexCond(node.path("Recheck Cond").asText(null))
                .predicateObservations(observations(node.path("Filter"), actual * loops, total))
                .build());
    }

    private static long rows(JsonNode node) { return node.path("Actual Rows").isNumber() ? node.path("Actual Rows").asLong() : 0; }
    private static long loops(JsonNode node) { return Math.max(1, node.path("Actual Loops").asLong(1)); }
    private static long rowsRemoved(JsonNode node, String key) { return node.path(key).isNumber() ? node.path(key).asLong() : 0; }

    private static List<PredicateObservation> observations(JsonNode filter, long matches, long input) {
        if (!filter.isTextual() || input == 0) return List.of();
        Optional<FilterExpression> parsed = FilterParser.parse(filter.asText());
        if (parsed.isEmpty()) return List.of();
        FilterExpression expression = parsed.get();
        if (expression.getLogicOp() == FilterExpression.LogicOp.OR) return List.of();
        List<FilterClause> atoms = new ArrayList<>();
        if (!collectConjunctiveAtoms(expression, atoms) || atoms.isEmpty()) return List.of();
        boolean lowerBound = atoms.size() > 1;
        List<PredicateObservation> result = new ArrayList<>();
        for (FilterClause atom : atoms) {
            if (!atom.isLiteral()) continue;
            result.add(PredicateObservation.builder().predicate(atom).matchingRows(matches)
                    .inputRows(input).lowerBound(lowerBound).build());
        }
        return result;
    }

    private static boolean collectConjunctiveAtoms(FilterExpression expression, List<FilterClause> out) {
        if (expression.getLogicOp() == FilterExpression.LogicOp.OR) return false;
        if (expression.getChildren() != null && !expression.getChildren().isEmpty()) {
            for (FilterExpression child : expression.getChildren()) if (!collectConjunctiveAtoms(child, out)) return false;
        } else if (expression.getClauses() != null) out.addAll(expression.getClauses());
        return true;
    } // WHERE status == "arrived" AND departure_time < ...
}
