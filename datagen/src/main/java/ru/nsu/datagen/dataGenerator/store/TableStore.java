package ru.nsu.datagen.dataGenerator.store;

import com.zaxxer.hikari.HikariDataSource;
import lombok.extern.slf4j.Slf4j;
import org.postgresql.util.PSQLException;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;

import java.io.StringReader;
import java.math.BigDecimal;
import java.sql.*;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;

import org.postgresql.util.PGobject;
import org.postgresql.core.BaseConnection;
import org.postgresql.copy.CopyManager;

@Slf4j
public class TableStore {
    public record StoreResult(int stored, List<Integer> failedIndices) {}

    private record ProcessChunkResult(int stored, List<Integer> failedIndices) {}

    private final HikariDataSource dataSource;
    private final int BATCHSIZE = 10000;
    private final int CHUNK_SIZE = 50000;

    public TableStore(HikariDataSource dataSource) {
        this.dataSource = dataSource;
    }




    private final DateTimeFormatter tsFormatter = DateTimeFormatter
            .ofPattern("yyyy-MM-dd HH:mm:ss.SSSX")
            .withZone(ZoneId.of("UTC"));





    public StoreResult storeTable(TableMetadata tableMetadata, Map<String, List<Object>> generatedTableData, int parallelism) throws SQLException {
        int batchRecords = generatedTableData.values().iterator().next().size();
        if (batchRecords == 0) return new StoreResult(0, List.of());

        int subBatchSize = Math.max(1, (batchRecords + parallelism - 1) / parallelism);
        try (ExecutorService executor = Executors.newVirtualThreadPerTaskExecutor()) {
            AtomicInteger totalStored = new AtomicInteger(0);
            AtomicInteger lastReportedPercent = new AtomicInteger(-1);

            log.info("Starting COPY store for table {} using Virtual Threads", tableMetadata.getTableName());

            List<CompletableFuture<ProcessChunkResult>> futures = new ArrayList<>();

            for (int i = 0; i < batchRecords; i += subBatchSize) {
                final int startIdx = i;
                final int endIdx = Math.min(startIdx + subBatchSize, batchRecords);

                futures.add(CompletableFuture.supplyAsync(() -> {
                    try {
                        return processCopyChunk(tableMetadata, generatedTableData, startIdx, endIdx, totalStored, lastReportedPercent);
                    } catch (Exception e) {
                        System.err.println(e.getMessage());
                        throw new RuntimeException("Error in virtual thread during COPY", e);
                    }
                }, executor));
            }

            List<Integer> allFailed = new ArrayList<>();
            int total = 0;
            for (CompletableFuture<ProcessChunkResult> future : futures) {
                try {
                    ProcessChunkResult r = future.join();
                    total += r.stored();
                    allFailed.addAll(r.failedIndices());
                } catch (CompletionException e) {
                    System.err.println(e.getMessage());
                    if (e.getCause() instanceof SQLException sqlEx) throw sqlEx;
                    throw new SQLException("Error during parallel COPY", e.getCause());
                }
            }
            log.info("Stored {}/{} rows in table {} ({} rejected)", total, batchRecords, tableMetadata.getTableName(), allFailed.size());
            return new StoreResult(total, allFailed);
        } catch (CompletionException e) {
            throw new SQLException("Parallel COPY failed in virtual threads", e.getCause());
        }
    }


    private ProcessChunkResult processCopyChunk(TableMetadata tableMetadata, Map<String, List<Object>> data,
                                int start, int end, AtomicInteger counter, AtomicInteger lastReportedPercent) throws Exception {
        String columns = String.join(", ", tableMetadata.getColumns().keySet());
        String copySql = String.format("COPY %s (%s) FROM STDIN WITH (FORMAT CSV, HEADER FALSE, NULL 'NULL_MARKER')",
                tableMetadata.getNamespace() + "." + tableMetadata.getTableName(), columns);

        List<String> colNames = new ArrayList<>(tableMetadata.getColumns().keySet());
        //System.err.println("ACT_DEP + " + data.get("actual_departure"));
//        for (Object obj : data.get("actual_departure")) {
//            if (obj != null) {
//                System.err.println("NOT NULL");
//            }
//        }
        List<Integer> remainingIndices = new ArrayList<>();
        for (int i = start; i < end; i++) {
            remainingIndices.add(i);
        }

        List<Integer> failedIndices = new ArrayList<>();
        int totalAdded = 0;

        while (!remainingIndices.isEmpty()) {
            StringBuilder csvBuffer = new StringBuilder();

            for (int idx : remainingIndices) {
                for (int j = 0; j < colNames.size(); j++) {
                    Object value = data.get(colNames.get(j)).get(idx);
                    ColumnMetadata meta = tableMetadata.getColumns().get(colNames.get(j));
                    csvBuffer.append(formatForCsv(value, meta));
                    if (j < colNames.size() - 1) csvBuffer.append(",");
                }
                csvBuffer.append("\n");
            }

            try (Connection conn = dataSource.getConnection()) {
                BaseConnection pgConn = conn.unwrap(BaseConnection.class);
                CopyManager copyManager = new CopyManager(pgConn);

                try {
                    copyManager.copyIn(copySql, new StringReader(csvBuffer.toString()));

                    int added = remainingIndices.size();
                    int total = counter.addAndGet(added);
                    log.debug("Chunk sub-part successfully copied. Added: {}, Total: {}", added, total);

                    reportCopyProgress(total, tableMetadata.getRecordCount(), tableMetadata.getTableName(), lastReportedPercent);

                    totalAdded += added;
                    break;

                } catch (SQLException e) {

                    if (e.getSQLState() != null && e.getSQLState().startsWith("23")) {
                        PSQLException pgEx = (PSQLException) e;

                        int lineInError = parseLineNumber(pgEx.getServerErrorMessage().getWhere());

                        if (lineInError > 0 && lineInError <= remainingIndices.size()) {
                            int badAbsoluteIndex = remainingIndices.get(lineInError - 1);
                            log.debug("Constraint violation in table {}, skipping row at absolute index {}. Constraint: {}",
                                    tableMetadata.getTableName(), badAbsoluteIndex, pgEx.getServerErrorMessage().getConstraint());
                            failedIndices.add(badAbsoluteIndex);
                            remainingIndices.remove(lineInError - 1);
                        } else {
                            log.error("Postgres reported lineInError={}, but current remaining size is {}. Cannot recover.",
                                    lineInError, remainingIndices.size());
                            throw e;
                        }
                    } else {
                        throw e;
                    }
                }
            }
        }

        return new ProcessChunkResult(totalAdded, failedIndices);
    }

    private int parseLineNumber(String whereClause) {
        if (whereClause == null || !whereClause.contains("line")) return -1;
        try {
//            log.warn("whereClause = {}", whereClause);
            String number = whereClause.replaceAll(".*line\\s+(\\d+).*", "$1");
//            log.warn("number = {}", number);
            return Integer.parseInt(number);
        } catch (Exception e) {
            System.err.println("catched exception during parsing " + e.getMessage() + " " + e.getCause());
            return -1;
        }
    }

    private String formatForCsv(Object value, ColumnMetadata meta) {
        if (value == null) {
            return "NULL_MARKER";
        }


        if (meta.isArray()) {
            StringBuilder arraySb = new StringBuilder();
            arraySb.append("{");

            if (value instanceof List<?> list) {
                for (int i = 0; i < list.size(); i++) {
                    arraySb.append(escapeArrayElement(list.get(i)));
                    if (i < list.size() - 1) arraySb.append(",");
                }
            } else if (value.getClass().isArray()) {
                Object[] arr = (Object[]) value;
                for (int i = 0; i < arr.length; i++) {
                    arraySb.append(escapeArrayElement(arr[i]));
                    if (i < arr.length - 1) arraySb.append(",");
                }
            } else {

                arraySb.append(escapeArrayElement(value));
            }

            arraySb.append("}");

            return "\"" + arraySb.toString() + "\"";
        }


        if (value instanceof String s) {
            return "\"" + s.replace("\"", "\"\"") + "\"";
        }


        if (value instanceof java.time.Instant instant) {
            return tsFormatter.format(instant);
        }


        return value.toString();
    }


    private String escapeArrayElement(Object element) {
        if (element == null) return "NULL";
        String s = element.toString();

        if (s.contains(",") || s.contains("\"") || s.contains("{") || s.contains("}")) {
            return "\"" + s.replace("\"", "\\\"") + "\"";
        }
        return s;
    }





    private void reportCopyProgress(int current, int total, String tableName, AtomicInteger lastReportedPercent) {
        int percent = (int) ((current * 100.0) / total);


        if (percent % 10 == 0 && percent > lastReportedPercent.get()) {

            int oldPercent = lastReportedPercent.get();
            if (percent > oldPercent && lastReportedPercent.compareAndSet(oldPercent, percent)) {
                log.info("{}% of data stored in table {}", percent, tableName);
            }
        }
    }


}