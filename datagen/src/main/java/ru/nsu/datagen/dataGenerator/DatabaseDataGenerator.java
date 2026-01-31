package ru.nsu.datagen.dataGenerator;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.DataGenerator;
import ru.nsu.datagen.dataGenerator.graph.DependencyGraph;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadataMaker;
import ru.nsu.datagen.dataGenerator.store.TableStore;

import java.sql.Connection;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

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
    public static void generateData(List<String[]> rawData, Connection conn) throws SQLException {
        // Get table metadata list
        List<TableMetadata> tableMetadataList = TableMetadataMaker.processRawTableMetadata(rawData);
        // Fill dependency graph
        DependencyGraph dependencyGraph = new DependencyGraph();
        tableMetadataList.forEach(dependencyGraph::addTable);
        // Get generation order
        dependencyGraph.buildDependencies();
        List<TableMetadata> generationOrder = dependencyGraph.getGenerationOrder();
        // Init table store
        TableStore tableStore = new TableStore(conn);
        // generate
        Map<String, Map<String, List<Object>>> generatedData = new HashMap<>();
        DataGenerator dataGenerator = new DataGenerator();
        for (TableMetadata table : generationOrder) {
            log.info("Generate table: " + table.getTableName());
            Map<String, List<Object>> generatedTableData = dataGenerator.generateTableData(table, generatedData);
            // TODO: надо распараллелить
            tableStore.storeTable(table, generatedTableData);
            generatedData.put(table.getTableName(), generatedTableData);
        }
        debugPrintData(generatedData);

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
