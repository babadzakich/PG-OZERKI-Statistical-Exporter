package ru.nsu.datagen.dataGenerator.model;

import com.opencsv.bean.CsvToBeanBuilder;
import lombok.extern.slf4j.Slf4j;
import org.jgrapht.Graph;
import org.jgrapht.alg.clique.BronKerboschCliqueFinder;
import org.jgrapht.graph.DefaultEdge;
import org.jgrapht.graph.SimpleGraph;

import java.io.Reader;
import java.util.*;
import java.util.stream.Collectors;

@Slf4j
public class TableMetadataMaker {
    public static List<TableMetadata> processTableMetadata(Reader reader) {
        // Читаем CSV и превращаем его в список объектов ColumnMetadataCSV
        List<ColumnMetadataCSV> csvData = new CsvToBeanBuilder<ColumnMetadataCSV>(reader)
                .withType(ColumnMetadataCSV.class)
                .withSeparator(',')
                .withIgnoreLeadingWhiteSpace(true)
                .withEscapeChar('\0')
                .build()
                .parse();

        // Строим графы для составных уникальных индексов и составных внешних ключей
        Map<String, List<Set<String>>> compositeUniquePeersMap = new HashMap<>(), compositeFkPeersMap = new HashMap<>();
        computeCompositePeers(csvData, compositeUniquePeersMap, compositeFkPeersMap);

        return csvData.stream()
                .collect(Collectors.groupingBy(ColumnMetadataCSV::getTableName)) // Группируем по имени таблицы наши колонки из CSV
                .entrySet().stream() // Тут мы получаем поток, где каждый энтри - это имя таблицы и список цсв колонок, относящихся к этой таблице
                .map(entry -> { // Тут мы мапаем каждую группу колонок в объект TableMetadata
                    String tableName = entry.getKey();
                    List<ColumnMetadataCSV> tableCsvColumns = entry.getValue();

                    Map<String, ColumnMetadata> columnMetadataMap = tableCsvColumns.stream() // Тут мы превращаем список колонок из CSV в мапу, где ключ - имя колонки, а значение - объект ColumnMetadata
                            .map(col ->
                                    col.transformToColumnMetadata(
                                            compositeUniquePeersMap.getOrDefault(
                                                col.getColumnName(), Collections.emptyList()
                                            ),
                                            compositeFkPeersMap.getOrDefault(
                                                col.getColumnName(), Collections.emptyList()
                                            )
                                    ))
                            .collect(Collectors.toMap(ColumnMetadata::getName, column -> column));

                    int recordCount = tableCsvColumns.isEmpty() ? 0 : tableCsvColumns.getFirst().getRecordCount();
                    String namespace = tableCsvColumns.isEmpty() ? "public" : tableCsvColumns.getFirst().getSchemaName();
                    Set<String> refTables = columnMetadataMap.keySet().stream()
                            .filter(colName -> columnMetadataMap.get(colName).isForeignKey())
                            .map(colName -> columnMetadataMap.get(colName).getForeignKeyMetadata().getFirst().getReferencedTable())
                            .collect(Collectors.toSet());
                    return new TableMetadata(tableName, columnMetadataMap, recordCount, namespace, refTables);
                })
                .collect(Collectors.toList());
    }

    private static void computeCompositePeers(List<ColumnMetadataCSV> csvData, Map<String, List<Set<String>>> compositeUniquePeersMap, Map<String, List<Set<String>>> compositeFkPeersMap) {
        Graph<String, DefaultEdge> uniqueGraph = new SimpleGraph<>(DefaultEdge.class), fkGraph = new SimpleGraph<>(DefaultEdge.class);

        csvData.forEach(csv -> {
            if (csv.getCompositePeers() != null && !csv.getCompositePeers().isEmpty() && !"NULL".equalsIgnoreCase(csv.getCompositePeers().trim())) {
                uniqueGraph.addVertex(csv.getColumnName().trim());
                log.debug("Added unique vertex: {}", csv.getColumnName().trim());
                log.debug("Composite unique peers for {}: {}", csv.getColumnName(), csv.getCompositePeers());
                for (String peer : csv.getCompositePeers().split(",")) {
                    if (!uniqueGraph.containsVertex(peer.trim())) uniqueGraph.addVertex(peer.trim());
                    uniqueGraph.addEdge(csv.getColumnName().trim(), peer.trim());
                    log.debug("Added unique edge: {} to {}", csv.getColumnName(), peer.trim());
                }
            }
            if (csv.getCompositePeers() != null && !csv.getCompositePeers().isEmpty() && !"NULL".equalsIgnoreCase(csv.getCompositePeers().trim())) {
                fkGraph.addVertex(csv.getColumnName().trim());
                log.debug("Added FK vertex: {}", csv.getColumnName().trim());
                log.debug("Composite FK peers for {}: {}", csv.getColumnName(), csv.getCompositeFkPeers());
                for (String peer : csv.getCompositeFkPeers().split(",")) {
                    if (!fkGraph.containsVertex(peer.trim())) fkGraph.addVertex(peer.trim());
                    fkGraph.addEdge(csv.getColumnName().trim(), peer.trim());
                    log.debug("Added FK edge: {} to {}", csv.getColumnName(), peer.trim());
                }
            }
        });
        BronKerboschCliqueFinder<String, DefaultEdge> uniqueFinder = new BronKerboschCliqueFinder<>(uniqueGraph),
                fkFinder = new BronKerboschCliqueFinder<>(fkGraph);

        // Отдельные составные уникальные индексы в табличках
        uniqueFinder.forEach(clique -> {
                log.debug("Found Unique clique: {}", clique);
                for (String vertex : clique) {
                    compositeUniquePeersMap.computeIfAbsent(vertex, k -> new ArrayList<>()).add(clique);
                }
            }
        );

        // Отдельные составные внешние ключи в табличках
        fkFinder.forEach(clique -> {
                log.debug("Found FK clique: {}", clique);
                for (String vertex : clique) {
                    compositeFkPeersMap.computeIfAbsent(vertex, k -> new ArrayList<>()).add(clique);
                }
        });
    }
}