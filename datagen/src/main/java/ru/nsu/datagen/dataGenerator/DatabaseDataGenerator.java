package ru.nsu.datagen.dataGenerator;

import com.zaxxer.hikari.HikariDataSource;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.DataGenerator;
import ru.nsu.datagen.dataGenerator.graph.DependencyGraph;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.dataGenerator.store.TableStore;

import java.sql.SQLException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;

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
    public static void generateData(List<TableMetadata> tableMetadataList, HikariDataSource dataSource) {
        // Fill dependency graph
        DependencyGraph dependencyGraph = new DependencyGraph();
        tableMetadataList.forEach(dependencyGraph::addTable);
        // Get generation order
        dependencyGraph.buildDependencies();
        List<TableMetadata> generationOrder = dependencyGraph.getGenerationOrder();
        // Init table store
        TableStore tableStore = new TableStore(dataSource);
        // generate
        Map<String, List<Object>> generatedData = new HashMap<>();
        Map<String, CompletableFuture<Void>> storeFutures = new HashMap<>();
        Map<String, TableMetadata> allTablesMap = new HashMap<>();
        for (TableMetadata t : tableMetadataList) {
            allTablesMap.put(t.getTableName(), t);
        }
        DataGenerator dataGenerator = new DataGenerator(allTablesMap);

        for (TableMetadata table : generationOrder) {
            log.info("Generate table: {}", table.getTableName());
            Map<String, List<Object>> generatedTableData = dataGenerator.generateTableData(table, generatedData);

            if (table.hasForeignKeyDependencies()) {
                List<CompletableFuture<Void>> dependencyFutures = table.getRefTables().stream()
                        .map(storeFutures::get)
                        .toList();
                storeFutures.put(table.getTableName(), CompletableFuture.allOf(
                        dependencyFutures.toArray(new CompletableFuture[0])
                ).thenRunAsync(() -> storeAsync(table, tableStore, generatedTableData))
                        .exceptionally(ex -> {
                            log.error("Exception during storing table: {}", table.getTableName(), ex);
                            return null;
                        }));
            } else {
                storeFutures.put(table.getTableName(), CompletableFuture.runAsync(() -> storeAsync(table, tableStore, generatedTableData))
                        .exceptionally(ex -> {
                    log.error("Exception during storing table: {}", table.getTableName(), ex);
                    return null;
                }));
            }
            for (String columnName : generatedTableData.keySet()) {
                if (!table.getColumns().get(columnName).getReferencingColumns().isEmpty()) {
                    generatedData.put(table.getNamespace() + '.' + table.getTableName() + "." + columnName, generatedTableData.get(columnName));
                }
            }
//            generatedData.put(table.getNamespace() + '.' + table.getTableName(), generatedTableData);
        }
        CompletableFuture.allOf(storeFutures.values().toArray(new CompletableFuture[0])).join();
        log.info("Data generation and storage completed.");
//        debugPrintData(generatedData);
    }

    private static void storeAsync(TableMetadata table, TableStore tableStore, Map<String, List<Object>> generatedTableData) {
        log.info("Async store table: {}", table.getTableName());
        try {
            tableStore.storeTable(table, generatedTableData);
        } catch (SQLException e) {
            throw new RuntimeException(e);
        }
    }

    private static void debugPrintData(Map<String, Map<String, List<Object>>> generatedData) {
        int size = 0;
        for (String tableName : generatedData.keySet()) {

            for (String columnName : generatedData.get(tableName).keySet()) {
                size = generatedData.get(tableName).get(columnName).size();
                break;

            }
            for (int i = 0; i < size; i++) {
                StringBuilder data = new StringBuilder("[");
                for (String columnName : generatedData.get(tableName).keySet()) {
                    data.append(generatedData.get(tableName).get(columnName).get(i)).append(", ");
                }
                data.append("]");
                log.trace("Table: {} Row {}: {}", tableName, i, data);
            }
        }
    }
}
