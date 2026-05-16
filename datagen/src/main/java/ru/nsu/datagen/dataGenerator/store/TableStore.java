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
    private final HikariDataSource dataSource;
    private final int BATCHSIZE = 10000;
    private final int CHUNK_SIZE = 50000;

    public TableStore(HikariDataSource dataSource) {
        this.dataSource = dataSource;
    }




    private final DateTimeFormatter tsFormatter = DateTimeFormatter
            .ofPattern("yyyy-MM-dd HH:mm:ss.SSSX")
            .withZone(ZoneId.of("UTC"));





    public int storeTable(TableMetadata tableMetadata, Map<String, List<Object>> generatedTableData, int parallelism) throws SQLException {
        System.err.println("storeTable call #1");
        int batchRecords = generatedTableData.values().iterator().next().size();
        System.err.println("Batch Records: " + batchRecords);
        if (batchRecords == 0) return 0;
        System.err.println("storeTable call #2");

        int subBatchSize = Math.max(1, (batchRecords + parallelism - 1) / parallelism);
        try (ExecutorService executor = Executors.newVirtualThreadPerTaskExecutor()) {
            AtomicInteger totalStored = new AtomicInteger(0);
            AtomicInteger lastReportedPercent = new AtomicInteger(-1);

            log.info("Starting COPY store for table {} using Virtual Threads", tableMetadata.getTableName());

            List<CompletableFuture<Integer>> futures = new ArrayList<>();

            for (int i = 0; i < batchRecords; i += subBatchSize) {
                final int startIdx = i;
                final int endIdx = Math.min(startIdx + subBatchSize, batchRecords);

                futures.add(CompletableFuture.supplyAsync(() -> {
                    try {
                        return processCopyChunk(tableMetadata, generatedTableData, startIdx, endIdx, totalStored, lastReportedPercent);
                    } catch (Exception e) {
                        throw new RuntimeException("Error in virtual thread during COPY", e);
                    }
                }, executor));
            }

            int total = 0;
            for (CompletableFuture<Integer> future : futures) {
                try {
                    total += future.join();
                } catch (CompletionException e) {
                    if (e.getCause() instanceof SQLException sqlEx) throw sqlEx;
                    throw new SQLException("Error during parallel COPY", e.getCause());
                }
            }
            log.info("Stored {}/{} rows in table {}", total, batchRecords, tableMetadata.getTableName());
            return total;
        } catch (CompletionException e) {
            throw new SQLException("Parallel COPY failed in virtual threads", e.getCause());
        }
    }


    private int processCopyChunk(TableMetadata tableMetadata, Map<String, List<Object>> data,
                                int start, int end, AtomicInteger counter, AtomicInteger lastReportedPercent) throws Exception {
        String columns = String.join(", ", tableMetadata.getColumns().keySet());
        String copySql = String.format("COPY %s (%s) FROM STDIN WITH (FORMAT CSV, HEADER FALSE, NULL 'NULL_MARKER')",
                tableMetadata.getNamespace() + "." + tableMetadata.getTableName(), columns);

        List<String> colNames = new ArrayList<>(tableMetadata.getColumns().keySet());

        List<Integer> remainingIndices = new ArrayList<>();
        for (int i = start; i < end; i++) {
            remainingIndices.add(i);
        }

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
                        log.warn("Constraint violation in table {}, retrying without bad row. Error: {}",
                                tableMetadata.getTableName(), e.getMessage());

                        int lineInError = parseLineNumber(pgEx.getServerErrorMessage().getWhere());


                        if (lineInError > 0 && lineInError <= remainingIndices.size()) {

                            int badAbsoluteIndex = remainingIndices.get(lineInError - 1);


                            Map<String, Object> badRow = new HashMap<>();
                            for (String colName : tableMetadata.getColumns().keySet()) {
                                badRow.put(colName, data.get(colName).get(badAbsoluteIndex));
                            }

                            log.error("!!! Constraint Violation detected and SKIPPED !!!");
                            log.error("Table: {}, Constraint: {}", tableMetadata.getTableName(), pgEx.getServerErrorMessage().getConstraint());
                            log.error("Row number in current attempt: {}, Absolute index: {}", lineInError, badAbsoluteIndex);
                            log.error("Culprit Row Data: {}", badRow);


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

        return totalAdded;
    }

    private int parseLineNumber(String whereClause) {
        if (whereClause == null || !whereClause.contains("line")) return -1;
        try {
            log.warn("whereClause = {}", whereClause);
            String number = whereClause.replaceAll(".*line\\s+(\\d+).*", "$1");
            log.warn("number = {}", number);
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

    private void processChunk(TableMetadata tableMetadata, Map<String, List<Object>> data,
                              int start, int end, AtomicInteger totalCounter, int logStep) throws SQLException {

        String query = getQueryString(tableMetadata);

        try (Connection conn = dataSource.getConnection()) {
            conn.setAutoCommit(false);

            try (Statement st = conn.createStatement()) {
                st.execute("SET search_path TO public, bookings");
            }

            try (PreparedStatement pstmnt = conn.prepareStatement(query)) {
                int localBatchCount = 0;

                for (int i = start; i < end; i++) {
                    int colIdx = 1;
                    for (String columnName : tableMetadata.getColumns().keySet()) {
                        Object value = data.get(columnName).get(i);
                        ColumnMetadata meta = tableMetadata.getColumns().get(columnName);
                        setParameter(pstmnt, colIdx++, value, meta, conn);
                    }

                    pstmnt.addBatch();
                    localBatchCount++;

                    if (localBatchCount % BATCHSIZE == 0) {
                        pstmnt.executeBatch();
                        reportProgress(totalCounter, BATCHSIZE, logStep, tableMetadata.getTableName());
                    }
                }

                pstmnt.executeBatch();
                int remaining = localBatchCount % BATCHSIZE;
                if (remaining > 0 || localBatchCount < BATCHSIZE) {
                    reportProgress(totalCounter, (localBatchCount % BATCHSIZE == 0 && localBatchCount > 0) ? 0 : localBatchCount % BATCHSIZE, logStep, tableMetadata.getTableName());
                }

                conn.commit();
            } catch (SQLException e) {
                conn.rollback();
                throw e;
            }
        }
    }

    private void reportProgress(AtomicInteger counter, int delta, int step, String tableName) {
        int current = counter.addAndGet(delta);

        if (step > 0 && current % step < delta) {
            log.info("Progress for {}: ~{} rows stored", tableName, current);
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

    private String getQueryString(TableMetadata tableMetadata) {
        StringBuilder stringBuilder = new StringBuilder();
        stringBuilder.append("INSERT INTO ");
        stringBuilder.append(tableMetadata.getTableName());
        stringBuilder.append(" (");
        int i = 1;
        for (String columnName : tableMetadata.getColumns().keySet()) {
            stringBuilder.append(columnName);
            if (i++ < tableMetadata.getColumns().size()) {
                stringBuilder.append(", ");
            }
        }
        stringBuilder.append(") VALUES (");
        for (i = 1; i <= tableMetadata.getColumns().size(); i++) {
            stringBuilder.append("?");
            if (i < tableMetadata.getColumns().size()) {
                stringBuilder.append(", ");
            }
        }
        stringBuilder.append(")");
        log.info("Insert data in table {}", tableMetadata.getTableName());
        log.debug("Insert query: {}", stringBuilder);
        return stringBuilder.toString();
    }

    private void setParameter(PreparedStatement pstmnt, int idx, Object value, ColumnMetadata meta, Connection conn) throws SQLException {
        String type = meta.getDataType();
        String normalizedType = type.toLowerCase();

        if (value == null) {
            pstmnt.setNull(idx, Types.OTHER);
            return;
        }


        if (meta.isArray()) {
            String baseType = type.contains("[]") ? type.substring(0, type.indexOf("[]")) : type;
            Array sqlArray = conn.createArrayOf(baseType, new Object[]{value});
            pstmnt.setArray(idx, sqlArray);
            return;
        }


        if (normalizedType.equals("interval") || normalizedType.contains("tstzrange") ||
                normalizedType.contains("date") || normalizedType.contains("money")) {
            PGobject pgObject = new PGobject();
            pgObject.setType(normalizedType.contains("tstzrange") ? "tstzrange" :
                    normalizedType.contains("date") ? "date" :
                            normalizedType.contains("money") ? "money" : "interval");
            pgObject.setValue(value.toString());
            pstmnt.setObject(idx, pgObject);
            return;
        }


        if (normalizedType.equals("time without time zone") || normalizedType.equals("time")) {
            pstmnt.setObject(idx, value, Types.TIME);
        } else if (normalizedType.equals("timestamp without time zone") || normalizedType.equals("timestamp")) {
            if (value instanceof java.time.Instant instant) {
                pstmnt.setTimestamp(idx, Timestamp.from(instant));
            } else {
                pstmnt.setObject(idx, value, Types.TIMESTAMP);
            }
        } else if (normalizedType.equals("timestamp with time zone") || normalizedType.equals("timestamptz")) {
            if (value instanceof java.time.Instant instant) {
                pstmnt.setObject(idx, instant, Types.TIMESTAMP_WITH_TIMEZONE);
            } else if (value instanceof String s) {
                PGobject pgObject = new PGobject();
                pgObject.setType("timestamptz");
                pgObject.setValue(s);
                pstmnt.setObject(idx, pgObject);
            } else {
                pstmnt.setObject(idx, value);
            }
        }

        else if (value instanceof String && !normalizedType.contains("char")) {
            switch (normalizedType) {
                case "smallint", "int2" -> pstmnt.setShort(idx, Short.parseShort((String) value));
                case "integer", "int4" -> pstmnt.setInt(idx, Integer.parseInt((String) value));
                case "bigint", "int8" -> pstmnt.setLong(idx, Long.parseLong((String) value));
                case "boolean", "bool" -> pstmnt.setBoolean(idx, Boolean.parseBoolean((String) value));
                case "decimal", "numeric" -> pstmnt.setBigDecimal(idx, new BigDecimal((String) value));
                case "float8", "double", "double precision" -> pstmnt.setDouble(idx, Double.parseDouble((String) value));
                case "float4", "real" -> pstmnt.setFloat(idx, Float.parseFloat((String) value));
                case "bytea" -> pstmnt.setBytes(idx, ((String) value).getBytes());
                default -> pstmnt.setObject(idx, value);
            }
        } else {
            pstmnt.setObject(idx, value);
        }
    }
}