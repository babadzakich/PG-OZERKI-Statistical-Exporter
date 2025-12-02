package ru.nsu.datagen.dataGenerator;

import ru.nsu.datagen.dataGenerator.generators.DataGenerator;
import ru.nsu.datagen.dataGenerator.graph.DependencyGraph;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadataMaker;

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
public class DatabaseDataGenerator {
    public static void generateData(List<String[]> rawData) {
        // Get table metadata list
        List<TableMetadata> tableMetadataList = TableMetadataMaker.processRawTableMetadata(rawData);
        // Fill dependency graph
        DependencyGraph dependencyGraph = new DependencyGraph();
        tableMetadataList.forEach(table -> dependencyGraph.addTable(table));
        // Get generation order
        dependencyGraph.buildDependencies();
        List<TableMetadata> generationOrder = dependencyGraph.getGenerationOrder();
        // generate
        Map<String, Map<String, List<Object>>> generatedData = new HashMap<>();
        DataGenerator dataGenerator = new DataGenerator();
        for (TableMetadata table : generationOrder) {
            System.out.println("Generate table: " + table.getTableName());
            Map<String, List<Object>> generatedTableData = dataGenerator.generateTableData(table, generatedData);
            generatedData.put(table.getTableName(), generatedTableData);
        }
        debugPrintData(generatedData);

    }

    private static void debugPrintData(Map<String, Map<String, List<Object>>> generatedData) {
//        for (Map.Entry<String, Map<String, List<Object>>> tableEntry : generatedData.entrySet()) {
//            System.out.println("\nTable: " + tableEntry.getKey() + " rows num: " + generatedData.entrySet().size());
//            Map<String, List<Object>> tableData = tableEntry.getValue();
//
//            for (Map.Entry<String, List<Object>> columnEntry : tableData.entrySet()) {
//                System.out.println("  " + columnEntry.getKey() + ": " + columnEntry.getValue());
//            }
//        }
        for (String tableName : generatedData.keySet()) {
            for (int i = 0; i < 500; i++) {
                StringBuilder data = new StringBuilder("[");
                for (String columnName : generatedData.get(tableName).keySet()) {
                    data.append(generatedData.get(tableName).get(columnName).get(i) + ", ");
                }
                data.append("]");
                System.out.println("Table: " + tableName + " Row " + i + ": " + data.toString());
            }
        }
    }
}
