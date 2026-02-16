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
        List<ColumnMetadataCSV> csvData = new CsvToBeanBuilder<ColumnMetadataCSV>(reader)
                .withType(ColumnMetadataCSV.class)
                .withSeparator(',')
                .withIgnoreLeadingWhiteSpace(true)
                .withEscapeChar('\0')
                .build()
                .parse();
        Map<String, List<Set<String>>> compositePeersMap = new HashMap<>();
        Graph<String, DefaultEdge> graph = new SimpleGraph<>(DefaultEdge.class);
        csvData.forEach(csv -> {
            if (csv.getCompositePeers() != null && !csv.getCompositePeers().isEmpty() && !"NULL".equalsIgnoreCase(csv.getCompositePeers().trim())) {
                graph.addVertex(csv.getColumnName().trim());
                log.debug("Added vertex: {}", csv.getColumnName().trim());
                log.debug("Composite peers for {}: {}", csv.getColumnName(), csv.getCompositePeers());
                for (String peer : csv.getCompositePeers().split(",")) {
                    if (!graph.containsVertex(peer.trim())) graph.addVertex(peer.trim());
                    graph.addEdge(csv.getColumnName().trim(), peer.trim());
                    log.debug("Added edge: {} to {}", csv.getColumnName(), peer.trim());
                }
            }
        });
        BronKerboschCliqueFinder<String, DefaultEdge> finder = new BronKerboschCliqueFinder<>(graph);
        finder.forEach(clique -> {
                log.debug("Found clique: {}", clique);
                for (String vertex : clique) {
                    compositePeersMap.computeIfAbsent(vertex, k -> new ArrayList<>()).add(clique);
                }
            }
        );



        return csvData.stream()
                .collect(Collectors.groupingBy(ColumnMetadataCSV::getTableName))
                .entrySet().stream()
                .map(entry -> {
                    String tableName = entry.getKey();
                    List<ColumnMetadataCSV> tableCsvColumns = entry.getValue();

                    Map<String, ColumnMetadata> columnMetadataMap = tableCsvColumns.stream()
                            .map(col -> col.transformToColumnMetadata(compositePeersMap.getOrDefault(col.getColumnName(), Collections.emptyList())))
                            .collect(Collectors.toMap(ColumnMetadata::getName, column -> column));

                    int recordCount = tableCsvColumns.isEmpty() ? 0 : tableCsvColumns.getFirst().getRecordCount();
                    String namespace = tableCsvColumns.isEmpty() ? "public" : tableCsvColumns.getFirst().getSchemaName();
                    Set<String> refTables = columnMetadataMap.keySet().stream()
                            .filter(colName -> columnMetadataMap.get(colName).isForeignKey())
                            .map(colName -> columnMetadataMap.get(colName).getForeignKeyMetadata().getReferencedTable())
                            .collect(Collectors.toSet());
                    return new TableMetadata(tableName, columnMetadataMap, recordCount, namespace, refTables);
                })
                .collect(Collectors.toList());
    }
}