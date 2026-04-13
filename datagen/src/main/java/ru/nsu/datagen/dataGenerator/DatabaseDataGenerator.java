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
                    DataGenerator dataGenerator = new DataGenerator(component);
                    List<List<TableMetadata>> generationOrder = dependencyGraph.getGenerationOrder(component);
                    Map<String, List<Object>> generatedData = new ConcurrentHashMap<>();

                    CompletableFuture<Void> future = CompletableFuture.completedFuture(null);
                    for (List<TableMetadata> level : generationOrder) {
                        future = future.thenCompose(ignored -> {
                            List<CompletableFuture<Void>> levelFutures = level.stream()
                                    .map(table -> CompletableFuture.runAsync(() -> {
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
                                                log.debug("Failed to store table: {}", table.getTableName(), e);
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
    
    public static void generateData(Map<String, TableMetadata> tableMetadataList, HikariDataSource dataSource) {
        // Fill dependency graph
        DependencyGraph dependencyGraph = new DependencyGraph(tableMetadataList);
        // Get generation order
        List<Set<TableMetadata>> generationOrder = dependencyGraph.getWeaklyConnectedComponents();
        List<TableMetadata> orderedTables = new ArrayList<>();
        generationOrder.forEach(orderedTables::addAll);
        // Init table store
        TableStore tableStore = new TableStore(dataSource);
        // generate
        Map<String, List<Object>> generatedData = new ConcurrentHashMap<>();
        Map<String, Integer> referenceCounters = new ConcurrentHashMap<>();
        tableMetadataList.forEach((tableName, table) ->
                table.getColumns().forEach((colName, col) -> {
                    if (col.getReferencingColumns() != null) {
                        String key = table.getFullName() + "." + colName;
                        referenceCounters.put(key, col.getReferencingColumns().size());
                    }
                })
        );
        Map<String, CompletableFuture<Void>> storeFutures = new HashMap<>();
        DataGenerator dataGenerator = new DataGenerator(tableMetadataList);

        for (TableMetadata table : orderedTables) {
            log.info("Generate table: {}", table.getTableName());
            Map<String, List<Object>> generatedTableData = dataGenerator.generateTableData(table, generatedData);

            for (String columnName : generatedTableData.keySet()) {
                if (table.getColumns().get(columnName).getReferencingColumns() != null) {
                    generatedData.put(table.getFullName() + "." + columnName, generatedTableData.get(columnName));
                }
            }

            Runnable evictParents = () -> table.getColumns().forEach((colName, col) -> {
                if (col.getForeignKeyMetadata() != null) {
                    for (var fk : col.getForeignKeyMetadata()) {
                        String parentKey = fk.getReferencedSchema() + "." +
                                fk.getReferencedTable() + "." +
                                fk.getReferencedColumn();
                        referenceCounters.computeIfPresent(parentKey, (k, count) -> {
                            int newCount = count - 1;
                            if (newCount <= 0) {
                                generatedData.remove(k);
                                log.info("Evicted parent data for key: {}", k);
                            }
                            return newCount;
                        });
                    }
                }
            });

            if (table.hasForeignKeyDependencies()) {
                List<CompletableFuture<Void>> dependencyFutures = table.getRefTables().stream()
                        .map(refTable -> {
                            log.info("Generate dependency table: {}", refTable);
                            CompletableFuture<Void> future = storeFutures.get(refTable);
                            if (future == null) {
                                log.error("Missing future for dependency table: {}", refTable);
                            }
                            return storeFutures.get(refTable);
                        })
                        .filter(Objects::nonNull).toList();

                storeFutures.put(table.getTableName(), CompletableFuture.allOf(dependencyFutures.toArray(CompletableFuture[]::new))
                        .thenRunAsync(() -> storeAsync(table, tableStore, generatedTableData))
                                .thenRun(evictParents)
                        .handle((result, ex) -> {
                            if (ex != null) {
                                log.error("Exception during storing table: {}", table.getTableName(), ex);
                                throw new CompletionException("Failed to store table: " + table.getTableName(), ex);
                            }
                            return result;
                        }));
            } else {
                storeFutures.put(table.getTableName(), CompletableFuture.runAsync(() -> storeAsync(table, tableStore, generatedTableData))
                                .thenRun(evictParents)
                    .handle((result, ex) -> {
                        if (ex != null) {
                            log.error("Exception during storing table: {}", table.getTableName(), ex);
                            throw new CompletionException("Failed to store table: " + table.getTableName(), ex);
                        }
                        return result;
                    }));
            }
        }
        CompletableFuture.allOf(storeFutures.values().toArray(CompletableFuture[]::new)).join();
        log.info("Data generation and storage completed.");
    }

    private static void storeAsync(TableMetadata table, TableStore tableStore, Map<String, List<Object>> generatedTableData) {
        log.info("Async store table: {}", table.getTableName());
        try {
            tableStore.storeTable(table, generatedTableData);
        } catch (SQLException e) {
            throw new RuntimeException(e);
        }
    }
}
