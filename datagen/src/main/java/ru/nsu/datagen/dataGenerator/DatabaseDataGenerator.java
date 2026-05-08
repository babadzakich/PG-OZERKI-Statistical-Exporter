package ru.nsu.datagen.dataGenerator;

import java.sql.SQLException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;

import com.zaxxer.hikari.HikariDataSource;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.DataGenerator;
import ru.nsu.datagen.dataGenerator.graph.DependencyGraph;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.dataGenerator.store.TableStore;

/*
TODO:
  1. написать пайплайн генерации данных
  2. приведение импортированных данных к HashMap'е
  3. store data from HashMap
  -----
  шаги пайплайна:
  1. запустить скрипт
 */
@Slf4j
public class DatabaseDataGenerator {
    public static void generateData(Map<String, TableMetadata> tableMetadataList, HikariDataSource dataSource, ExecutorService executorService, int batchSize) {
        // Fill dependency graph
        DependencyGraph dependencyGraph = new DependencyGraph(tableMetadataList);
        // Get generation order
        List<Set<TableMetadata>> components = dependencyGraph.getWeaklyConnectedComponents();
        // Init table store
        TableStore tableStore = new TableStore(dataSource);

        List<CompletableFuture<Void>> storeFutures = components.stream()
                .map(component -> {
                    List<List<TableMetadata>> generationOrder = dependencyGraph.getGenerationOrder(component);
                    Map<String, List<Object>> generatedData = new ConcurrentHashMap<>();

                    CompletableFuture<Void> future = CompletableFuture.completedFuture(null);
                    for (List<TableMetadata> level : generationOrder) {
                        future = future.thenCompose(ignored -> {
                            List<CompletableFuture<Void>> levelFutures = level.stream()
                                    .map(table -> CompletableFuture.runAsync(() -> {
                                        DataGenerator dataGenerator = new DataGenerator(component, table);
                                        log.info("Generate table: {}", table.getTableName());
                                        int createdAmount = 0;
                                        while (createdAmount < table.getRecordCount()) {
                                            int toGenerate = Math.min(batchSize, table.getRecordCount() - createdAmount);
                                            Map<String, List<Object>> generatedTableData = dataGenerator.generateBatchTableData(table, generatedData, toGenerate);
                                            generatedTableData.keySet().stream().filter(col -> table.getColumns().get(col).getReferencingColumns() != null).forEach(colName ->
                                                    generatedData.computeIfAbsent(table.getFullName() + "." + colName, k -> new ArrayList<>()).addAll(generatedTableData.get(colName))
                                            );
                                            try {
                                                tableStore.storeTable(table, generatedTableData);
                                                createdAmount += toGenerate;
                                            } catch (SQLException e) {
                                                dataGenerator.rollbackToPreviousState();
                                                log.warn(
                                                        "Failed to store batch for table {} from offset {} with size {}. sqlState={}, message={}",
                                                        table.getTableName(),
                                                        createdAmount,
                                                        toGenerate,
                                                        e.getSQLState(),
                                                        e.getMessage(),
                                                        e
                                                );
                                            }
                                        }
                                    }, executorService)).toList();

                            return CompletableFuture.allOf(levelFutures.toArray(CompletableFuture[]::new));
                        });
                    }

                    return future;
                }).toList(); 

        CompletableFuture.allOf(storeFutures.toArray(CompletableFuture[]::new)).join();
        log.info("Data generation and storage completed.");
    }
}
