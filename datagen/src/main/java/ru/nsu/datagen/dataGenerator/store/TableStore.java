package ru.nsu.datagen.dataGenerator.store;

import com.zaxxer.hikari.HikariDataSource;
import lombok.extern.slf4j.Slf4j;
import org.postgresql.copy.CopyManager;
import org.postgresql.core.BaseConnection;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;

import java.io.StringReader;
import java.sql.BatchUpdateException;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

@Slf4j
public class TableStore {
    private static final String NULL_MARKER = "NULL_MARKER";

    private final HikariDataSource dataSource;
    private final int chunkSize = 50000;
    private final DateTimeFormatter tsFormatter = DateTimeFormatter
            .ofPattern("yyyy-MM-dd HH:mm:ss.SSSX")
            .withZone(ZoneId.of("UTC"));

    public TableStore(HikariDataSource dataSource) {
        this.dataSource = dataSource;
    }

    public void storeTable(TableMetadata tableMetadata, Map<String, List<Object>> generatedTableData) throws SQLException {
        int totalRecords = tableMetadata.getRecordCount();
        if (totalRecords == 0) {
            return;
        }

        try (ExecutorService executor = Executors.newVirtualThreadPerTaskExecutor()) {
            AtomicInteger totalStored = new AtomicInteger(0);
            AtomicInteger lastReportedPercent = new AtomicInteger(-1);
            List<CompletableFuture<Void>> futures = new ArrayList<>();

            log.info("Starting COPY store for table {} using Virtual Threads", tableMetadata.getTableName());

            for (int startIdx = 0; startIdx < totalRecords; startIdx += chunkSize) {
                final int chunkStart = startIdx;
                final int chunkEnd = Math.min(chunkStart + chunkSize, totalRecords);
                futures.add(CompletableFuture.runAsync(() ->
                        processCopyChunk(tableMetadata, generatedTableData, chunkStart, chunkEnd, totalStored, lastReportedPercent), executor));
            }

            CompletableFuture.allOf(futures.toArray(CompletableFuture[]::new)).join();
            log.info("100% of data stored in table {}, total {} rows", tableMetadata.getTableName(), totalRecords);
        } catch (CompletionException e) {
            Throwable cause = e.getCause();
            if (cause instanceof SQLException sqlException) {
                throw sqlException;
            }
            throw new SQLException("Parallel COPY failed in virtual threads", cause);
        }
    }

    private void processCopyChunk(
            TableMetadata tableMetadata,
            Map<String, List<Object>> data,
            int start,
            int end,
            AtomicInteger counter,
            AtomicInteger lastReportedPercent
    ) {
        String columns = String.join(", ", tableMetadata.getColumns().keySet());
        String copySql = String.format(
                "COPY %s (%s) FROM STDIN WITH (FORMAT CSV, HEADER FALSE, NULL '%s')",
                tableMetadata.getNamespace() + "." + tableMetadata.getTableName(),
                columns,
                NULL_MARKER
        );

        StringBuilder csvBuffer = new StringBuilder();
        List<String> colNames = new ArrayList<>(tableMetadata.getColumns().keySet());
        for (int rowIdx = start; rowIdx < end; rowIdx++) {
            for (int colIdx = 0; colIdx < colNames.size(); colIdx++) {
                String columnName = colNames.get(colIdx);
                Object value = data.get(columnName).get(rowIdx);
                ColumnMetadata meta = tableMetadata.getColumns().get(columnName);
                csvBuffer.append(formatForCsv(value, meta));
                if (colIdx < colNames.size() - 1) {
                    csvBuffer.append(",");
                }
            }
            csvBuffer.append("\n");
        }

        try (Connection conn = dataSource.getConnection()) {
            try (Statement statement = conn.createStatement()) {
                statement.execute("SET search_path TO public, bookings");
            }

            BaseConnection pgConn = conn.unwrap(BaseConnection.class);
            CopyManager copyManager = new CopyManager(pgConn);
            copyManager.copyIn(copySql, new StringReader(csvBuffer.toString()));

            int added = end - start;
            int total = counter.addAndGet(added);
            log.debug("Chunk {}-{} copied. Total: {}", start, end, total);
            reportCopyProgress(total, tableMetadata.getRecordCount(), tableMetadata.getTableName(), lastReportedPercent);
        } catch (SQLException e) {
            logSqlException(tableMetadata, copySql, e);
            throw new CompletionException(e);
        } catch (Exception e) {
            log.error(
                    "Failed to build or copy chunk for table {}. range=[{}, {}), message={}",
                    tableMetadata.getTableName(),
                    start,
                    end,
                    e.getMessage(),
                    e
            );
            throw new CompletionException(e);
        }
    }

    private void logSqlException(TableMetadata tableMetadata, String sql, SQLException e) {
        log.error(
                "Failed to store data in table {}. sql='{}', exceptionType={}, sqlState={}, errorCode={}, message={}",
                tableMetadata.getTableName(),
                sql,
                e.getClass().getName(),
                e.getSQLState(),
                e.getErrorCode(),
                e.getMessage(),
                e
        );

        if (e instanceof BatchUpdateException batchException) {
            log.error(
                    "Batch execution details for table {}: updateCounts={}",
                    tableMetadata.getTableName(),
                    Arrays.toString(batchException.getUpdateCounts())
            );
        }

        SQLException nextException = e.getNextException();
        int nextIndex = 1;
        while (nextException != null) {
            log.error(
                    "Next SQL exception #{} for table {}: exceptionType={}, sqlState={}, errorCode={}, message={}",
                    nextIndex,
                    tableMetadata.getTableName(),
                    nextException.getClass().getName(),
                    nextException.getSQLState(),
                    nextException.getErrorCode(),
                    nextException.getMessage(),
                    nextException
            );
            nextException = nextException.getNextException();
            nextIndex++;
        }
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

    private String formatForCsv(Object value, ColumnMetadata meta) {
        if (value == null) {
            return NULL_MARKER;
        }

        if (meta.isArray()) {
            StringBuilder arraySb = new StringBuilder();
            arraySb.append("{");

            if (value instanceof List<?> list) {
                for (int i = 0; i < list.size(); i++) {
                    arraySb.append(escapeArrayElement(list.get(i)));
                    if (i < list.size() - 1) {
                        arraySb.append(",");
                    }
                }
            } else if (value.getClass().isArray()) {
                Object[] arr = (Object[]) value;
                for (int i = 0; i < arr.length; i++) {
                    arraySb.append(escapeArrayElement(arr[i]));
                    if (i < arr.length - 1) {
                        arraySb.append(",");
                    }
                }
            } else {
                arraySb.append(escapeArrayElement(value));
            }

            arraySb.append("}");
            return "\"" + arraySb + "\"";
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
        if (element == null) {
            return "NULL";
        }

        String s = element.toString();
        if (s.contains(",") || s.contains("\"") || s.contains("{") || s.contains("}")) {
            return "\"" + s.replace("\"", "\\\"") + "\"";
        }
        return s;
    }
}
