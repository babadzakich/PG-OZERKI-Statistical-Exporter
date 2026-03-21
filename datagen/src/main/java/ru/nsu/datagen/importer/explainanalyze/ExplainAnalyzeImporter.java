package ru.nsu.datagen.importer.explainanalyze;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.model.AnalyzePlanInfo;

import java.io.File;
import java.io.IOException;
import java.util.*;

@Slf4j
public class ExplainAnalyzeImporter {
    public static Map<String, List<AnalyzePlanInfo>> importExplainAnalyze(String filePath) {
        Map<String, List<AnalyzePlanInfo>> result = null;
        try {
            ObjectMapper mapper = new ObjectMapper();
            JsonNode rootNode = mapper.readTree(new File(filePath));

            JsonNode planRoot = rootNode.isArray()
                    ? rootNode.get(0).get("Plan")
                    : rootNode.get("Plan");
            result = traverse(planRoot);
        } catch (IOException e){
            log.error("Error during reading JSON tree: {}", e.getMessage());
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

        long actual  = node.path("Actual Rows").asLong(0);
        long removed = node.path("Rows Removed by Filter").asLong(0);
        long total   = actual + removed;

        return Optional.of(AnalyzePlanInfo.builder()
                .tableName(node.path("Relation Name").asText())
                .scanType(ScanType.SEQ_SCAN)
                .actualRows(actual)
                .totalRows(total)
                .selectivity(total > 0 ? (double) actual / total : 1.0)
                .rawFilter(node.path("Filter").asText(null))
                .build());
    }

    private static Optional<AnalyzePlanInfo> handleIndexScan(JsonNode node) {
        if (!node.has("Relation Name")) return Optional.empty();

        long actual  = node.path("Actual Rows").asLong(0);
        long loops   = node.path("Actual Loops").asLong(1);
        long removed = node.path("Rows Removed by Index Recheck").asLong(0);

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
                .build());
    }

    private static Optional<AnalyzePlanInfo> handleBitmapScan(JsonNode node) {
        if (!node.has("Relation Name")) return Optional.empty();

        long actual  = node.path("Actual Rows").asLong(0);
        long loops   = node.path("Actual Loops").asLong(1);
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
                .build());
    }
}