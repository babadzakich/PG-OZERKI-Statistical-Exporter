package ru.nsu.datagen.dataGenerator.graph;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

import org.jgrapht.Graph;
import org.jgrapht.alg.connectivity.ConnectivityInspector;
import org.jgrapht.graph.AsUndirectedGraph;
import org.jgrapht.graph.DefaultDirectedGraph;
import org.jgrapht.graph.DefaultEdge;

import ru.nsu.datagen.dataGenerator.model.TableMetadata;

/**
 * Граф FK-зависимостей между таблицами на основе JGraphT.
 *
 * <p>Используется для двух целей:
 * <ol>
 *   <li>{@link #getWeaklyConnectedComponents()} — разбивает граф на независимые компоненты,
 *       которые могут генерироваться параллельно.</li>
 *   <li>{@link #getGenerationOrder(Set)} — возвращает уровни топологической сортировки внутри
 *       компоненты: таблицы одного уровня не зависят друг от друга и генерируются параллельно;
 *       уровни выполняются строго последовательно (родители раньше детей).</li>
 * </ol>
 */
public class DependencyGraph {
    private final Map<String, TableDependency> tableNodes;
    private final Map<String, TableMetadata> allTables;

    /**
     * Строит граф по метаданным: рёбра направлены от дочерней таблицы к родительской
     * (по FK-ссылкам в {@link TableMetadata#getRefTables()}).
     *
     * @param allTables все таблицы схемы (ключ: {@code schema.tableName})
     */
    public DependencyGraph(Map<String, TableMetadata> allTables) {    
        this.allTables = allTables;
        this.tableNodes = allTables.entrySet().stream()
                .collect(HashMap::new, (map, entry) -> map.put(entry.getKey(), new TableDependency(entry.getValue())), HashMap::putAll);
        allTables.values().forEach(table -> {
            TableDependency currDependency = tableNodes.get(table.getFullName());
            table.getRefTables().forEach(refTableKey -> {
                tableNodes.get(refTableKey).addDependency(currDependency);
            });
        });
    }

    /**
     * Возвращает слабо связанные компоненты графа зависимостей.
     * Таблицы в разных компонентах не имеют общих FK-связей и могут генерироваться независимо.
     *
     * @return список компонент; каждая компонента — множество метаданных таблиц
     */
    public List<Set<TableMetadata>> getWeaklyConnectedComponents() {
        Graph<String, DefaultEdge> tablesGraph = new DefaultDirectedGraph<>(DefaultEdge.class);

        allTables.keySet().forEach(tablesGraph::addVertex);
        allTables.forEach((tableName, table) -> {
            table.getRefTables().forEach(refTable -> {
                if (allTables.containsKey(refTable)) {
                    tablesGraph.addVertex(refTable);
                    tablesGraph.addEdge(tableName, refTable);
                }
            });
        });

        ConnectivityInspector<String, DefaultEdge> inspector =
        new ConnectivityInspector<>(new AsUndirectedGraph<>(tablesGraph));

        List<Set<String>> components = inspector.connectedSets();
        List<Set<TableMetadata>> result = new ArrayList<>();
        components.forEach(comp -> 
            result.add(comp.stream().map(allTables::get).collect(HashSet::new, Set::add, Set::addAll))
        );
        return result;
    }

    /**
     * Вычисляет порядок генерации таблиц внутри компоненты через топологическую сортировку (BFS по входящим степеням).
     *
     * <p>Таблицы с нулевой входящей степенью (нет FK-родителей внутри компоненты) образуют
     * первый уровень. После их «обработки» входящие степени соседей уменьшаются,
     * что открывает следующий уровень.
     *
     * @param component подмножество таблиц одной слабо связанной компоненты
     * @return список уровней; таблицы одного уровня генерируются параллельно
     */
    public List<List<TableMetadata>> getGenerationOrder(Set<TableMetadata> component) {
        Set<TableDependency> componentNodes = component.stream()
                .map(table -> tableNodes.get(table.getFullName()))
                .collect(Collectors.toSet());

        Map<TableDependency, Integer> inDegree = new HashMap<>();
        componentNodes.forEach(node -> inDegree.put(node, 0));
        
        componentNodes.forEach(table -> {
            table.getDependencies().stream().filter(dep -> componentNodes.contains(dep))
            .forEach(dep -> inDegree.computeIfPresent(dep, (k, v) -> v + 1));
        });

        List<TableDependency> currentLevel = inDegree.entrySet().stream()
            .filter(e -> e.getValue() == 0)
            .map(Map.Entry::getKey)
            .toList();


        List<List<TableMetadata>> order = new ArrayList<>();

        while (!currentLevel.isEmpty()) {
            List<TableMetadata> currentLevelTables = new ArrayList<>();
            List<TableDependency> nextLevel = new ArrayList<>();
            for (TableDependency node : currentLevel) {
                currentLevelTables.add(node.getTable());
                for (TableDependency dependency : node.getDependencies()) {
                    if (!componentNodes.contains(dependency)) {
                        continue;
                    }
                    int newInDegree = inDegree.computeIfPresent(dependency, (k,v) -> v - 1);
                    if (newInDegree == 0) {
                        nextLevel.add(dependency);
                    }
                }
            }
            order.add(currentLevelTables);
            currentLevel = nextLevel;
        }

        return order;
    }
}