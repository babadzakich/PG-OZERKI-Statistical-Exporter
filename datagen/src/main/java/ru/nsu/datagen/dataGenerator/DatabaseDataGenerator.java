package ru.nsu.datagen.dataGenerator;

import java.io.IOException;
import java.sql.SQLException;
import java.util.*;
import java.util.concurrent.*;
import java.util.LinkedHashMap;

import com.zaxxer.hikari.HikariDataSource;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.DataGenerator;
import ru.nsu.datagen.dataGenerator.graph.DependencyGraph;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.dataGenerator.store.TableStore;
import ru.nsu.datagen.dataGenerator.store.TableStore.StoreResult;

/**
 * Управляет параллельной генерацией и сохранением данных для всех таблиц.
 *
 * <p>Алгоритм:
 * <ol>
 *   <li>Строит {@link DependencyGraph} по FK-зависимостям между таблицами.</li>
 *   <li>Разбивает граф на слабо связанные компоненты — они генерируются независимо и параллельно.</li>
 *   <li>Внутри каждой компоненты таблицы генерируются уровнями топологической сортировки:
 *       родительские таблицы всегда завершены до дочерних.</li>
 *   <li>Данные каждой таблицы вставляются батчами через {@link TableStore} (PostgreSQL COPY FROM STDIN).</li>
 *   <li>Строки, нарушающие constraint, повторно генерируются Markov-генератором (до 3 попыток).</li>
 * </ol>
 *
 * <p>Сгенерированные значения FK-колонок накапливаются в {@code generatedData} (ключ:
 * {@code schema.table.column}) и используются дочерними таблицами при создании FK-ссылок.
 */
@Slf4j
public class DatabaseDataGenerator {
    private static final int MAX_EMPTY_BATCH_COUNT = 1000;

    /**
     * Запускает генерацию и сохранение данных для всех таблиц из {@code tableMetadataList}.
     *
     * @param tableMetadataList метаданные всех таблиц (ключ: {@code schema.tableName})
     * @param dataSource        пул соединений HikariCP
     * @param executorService   пул потоков для параллельной генерации таблиц
     * @param batchSize         максимальный размер одного батча генерации
     * @param globStoreThreads  максимальное число таблиц, одновременно пишущих в БД
     * @param tableStoreThreads число параллельных COPY-потоков на одну таблицу
     * @throws IOException при ошибке I/O (в текущей реализации не выбрасывается)
     */
    public static void generateData(Map<String, TableMetadata> tableMetadataList, HikariDataSource dataSource, ExecutorService executorService, int batchSize, int globStoreThreads, int tableStoreThreads) throws IOException {
        // Fill dependency graph
        DependencyGraph dependencyGraph = new DependencyGraph(tableMetadataList);
        // Get generation order
        List<Set<TableMetadata>> components = dependencyGraph.getWeaklyConnectedComponents();
        // Init table store
        TableStore tableStore = new TableStore(dataSource);
        Semaphore globalSemaphore = new Semaphore(globStoreThreads);
        List<CompletableFuture<Void>> storeFutures = components.stream()
                .map(component -> {
                    List<List<TableMetadata>> generationOrder = dependencyGraph.getGenerationOrder(component);
                    Map<String, List<Object>> generatedData = new ConcurrentHashMap<>();

                    CompletableFuture<Void> future = CompletableFuture.completedFuture(null);
                    for (List<TableMetadata> level : generationOrder) {
                        future = future.thenCompose(ignored -> {
                            List<CompletableFuture<Void>> levelFutures = level.stream()
                                    .map(table -> CompletableFuture.runAsync(() -> {

                                        try {
                                            globalSemaphore.acquire();

                                            DataGenerator dataGenerator = new DataGenerator(component, table);
                                            log.info("Generate table: {}", table.getTableName());
                                            int createdAmount = 0;
                                            int emptyBatchCount = 0;
                                            while (createdAmount < table.getRecordCount()) {
                                                int toGenerate = Math.min(batchSize, table.getRecordCount() - createdAmount);
                                                System.err.println("toGenerate = " + toGenerate);

                                                Map<String, List<Object>> generatedTableData = dataGenerator.generateBatchTableData(table, generatedData, toGenerate, emptyBatchCount);
                                                try {
                                                    StoreResult result = tableStore.storeTable(table, generatedTableData, tableStoreThreads);
                                                    int stored = result.stored();

                                                    // Retry rows that failed with constraint violations by regenerating Markov columns
                                                    List<Integer> failedIndices = new ArrayList<>(result.failedIndices());
                                                    Map<String, List<Object>> currentBatch = generatedTableData;
                                                    for (int retry = 0; retry < 3 && !failedIndices.isEmpty(); retry++) {
                                                        Map<String, List<Object>> retryBatch = extractRows(currentBatch, failedIndices);
                                                        dataGenerator.regenerateMarkovColumns(retryBatch);
                                                        StoreResult retryResult = tableStore.storeTable(table, retryBatch, 1);
                                                        stored += retryResult.stored();
                                                        failedIndices = new ArrayList<>(retryResult.failedIndices());
                                                        currentBatch = retryBatch;
                                                    }

                                                    dataGenerator.removeUnaddedColumns(toGenerate - stored);
                                                    createdAmount += stored;
                                                    if (stored > 0) {
                                                        generatedTableData.keySet().stream().filter(col -> table.getColumns().get(col).getReferencingColumns() != null).forEach(colName ->
                                                                generatedData.computeIfAbsent(table.getFullName() + "." + colName, k -> new ArrayList<>()).addAll(generatedTableData.get(colName))
                                                        );
                                                    }
                                                    if (stored == 0) {
                                                        if (++emptyBatchCount > MAX_EMPTY_BATCH_COUNT) {
                                                            log.error("Прервана генерация таблицы {}: {} пустых батчей подряд", table.getTableName(), emptyBatchCount);
                                                            break;
                                                        }
                                                    } else {
                                                        emptyBatchCount = 0;
                                                    }
                                                } catch (SQLException e) {
                                                    log.warn("SQL error storing batch for table {}: sqlState={}, message={}",
                                                            table.getTableName(), e.getSQLState(), e.getMessage());
                                                }
                                            }
                                        } catch (InterruptedException e) {
                                            log.error("Error in table {}: {}", table.getTableName(), e.getMessage());
                                            throw new RuntimeException(e);
                                        } catch (RuntimeException e) {
                                            log.error("Unexpected error generating table {}: {}", table.getTableName(), e.getMessage(), e);
                                            throw e;
                                        } finally {
                                            globalSemaphore.release();
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

    private static Map<String, List<Object>> extractRows(Map<String, List<Object>> data, List<Integer> indices) {
        Map<String, List<Object>> result = new LinkedHashMap<>();
        for (Map.Entry<String, List<Object>> entry : data.entrySet()) {
            List<Object> col = new ArrayList<>(indices.size());
            for (int idx : indices) col.add(entry.getValue().get(idx));
            result.put(entry.getKey(), col);
        }
        return result;
    }
}
