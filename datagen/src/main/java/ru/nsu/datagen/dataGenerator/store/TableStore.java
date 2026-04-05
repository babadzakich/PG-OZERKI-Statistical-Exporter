package ru.nsu.datagen.dataGenerator.store;

import com.zaxxer.hikari.HikariDataSource;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;

import java.io.StringReader;
import java.math.BigDecimal;
import java.sql.*;
import java.time.Instant;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;

import org.postgresql.util.PGobject;
import org.postgresql.core.BaseConnection;
import org.postgresql.copy.CopyManager;

@Slf4j
public class TableStore {
    private final HikariDataSource dataSource;
    private final int BATCHSIZE = 10000;
    private final int CHUNK_SIZE = 50000;
    private final int THREAD_COUNT = 8;

    public TableStore(HikariDataSource dataSource) {
        this.dataSource = dataSource;
    }

//    public void storeTable(TableMetadata tableMetadata, Map<String, List<Object>> generatedTableData) throws SQLException {
//        String queryString = getQueryString(tableMetadata);
//        int onePercent = tableMetadata.getRecordCount() / 100 + 1;
//        int progress = 0;
//        try (Connection conn = dataSource.getConnection()) {
//            try (Statement statement = conn.createStatement()) {
//                statement.execute("SET search_path TO public, bookings");
//            }
//            try (PreparedStatement pstmnt = conn.prepareStatement(queryString)) {
//                int batchCount = 0;
//                // Каждая строка
//                conn.setAutoCommit(false);
//                for (int i = 0; i < tableMetadata.getRecordCount(); i++) {
//                    // Каждая колонка
//                    int j = 0;
//                    for (String columnName : tableMetadata.getColumns().keySet()) {
//                        Object value = generatedTableData.get(columnName).get(i);
//                        ColumnMetadata columnMetadata = tableMetadata.getColumns().get(columnName);
//                        String type = columnMetadata.getDataType();
//                        String normalizedType = type.toLowerCase();
//
//                        // Обработка массивов
//                        if (columnMetadata.isArray()) {
//                            if (type.contains("[]")) {
//                                type = type.substring(0, type.indexOf("[]"));
//                            }
//                            Array sqlArray = conn.createArrayOf(type, new Object[]{value});
//                            pstmnt.setArray(++j, sqlArray);
//                        } else if (normalizedType.equals("interval")) {
//                            PGobject pgObject = new PGobject();
//                            pgObject.setType("interval");
//                            pgObject.setValue(value.toString());
//                            pstmnt.setObject(++j, pgObject);
//                        } else if (normalizedType.equals("time without time zone") || normalizedType.equals("time")) {
//                            pstmnt.setObject(++j, value, Types.TIME);
//                        } else if (normalizedType.equals("timestamp without time zone") || normalizedType.equals("timestamp")) {
//                            if (value instanceof java.time.Instant instant) {
//                                pstmnt.setTimestamp(++j, Timestamp.from(instant));
//                            } else {
//                                pstmnt.setObject(++j, value, Types.TIMESTAMP);
//                            }
//                        } else if (normalizedType.equals("timestamp with time zone") || normalizedType.equals("timestamptz")) {
//                            if (value instanceof java.time.Instant instant) {
//                                pstmnt.setObject(++j, instant, Types.TIMESTAMP_WITH_TIMEZONE);
//                            } else if (value instanceof String s) {
//                                PGobject pgObject = new PGobject();
//                                pgObject.setType("timestamptz");
//                                pgObject.setValue(s);
//                                pstmnt.setObject(++j, pgObject);
//                            } else {
//                                pstmnt.setObject(++j, value);
//                            }
//                        } else if (normalizedType.contains("tstzrange")) {
//                            PGobject pgObject = new PGobject();
//                            pgObject.setType("tstzrange");
//                            pgObject.setValue((String) value);
//                            pstmnt.setObject(++j, pgObject);
//                        } else if (normalizedType.contains("date")) {
//                            PGobject pgObject = new PGobject();
//                            pgObject.setType("date");
//                            pgObject.setValue(value.toString());
//                            pstmnt.setObject(++j, pgObject);
//                        } else if (normalizedType.contains("num")) {
//                            pstmnt.setObject(++j, value);
//                        } else if (normalizedType.contains("money")) {
//                            PGobject pgObject = new PGobject();
//                            pgObject.setType("money");
//                            pgObject.setValue(value.toString());
//                            pstmnt.setObject(++j, pgObject);
//                        } else if (value instanceof String && !normalizedType.contains("char")) {
//                            switch (normalizedType) {
//                                case "smallint":
//                                case "int2":
//                                    pstmnt.setShort(++j, Short.parseShort((String) value));
//                                    break;
//                                case "integer":
//                                case "int4":
//                                    pstmnt.setInt(++j, Integer.parseInt((String) value));
//                                    break;
//                                case "bigint":
//                                case "int8":
//                                    pstmnt.setLong(++j, Long.parseLong((String) value));
//                                    break;
//                                case "varchar":
//                                case "text":
//                                    pstmnt.setString(++j, (String) value);
//                                    break;
//                                case "boolean":
//                                case "bool":
//                                    pstmnt.setBoolean(++j, Boolean.parseBoolean((String) value));
//                                    break;
//                                case "decimal":
//                                case "numeric":
//                                    pstmnt.setBigDecimal(++j, new BigDecimal((String) value));
//                                    break;
//                                case "float8":
//                                case "double":
//                                case "double precision":
//                                    pstmnt.setDouble(++j, Double.parseDouble((String) value));
//                                    break;
//                                case "float4":
//                                case "real":
//                                    pstmnt.setFloat(++j, Float.parseFloat((String) value));
//                                    break;
//                                case "bytea":
//                                    pstmnt.setBytes(++j, ((String) value).getBytes());
//                                    break;
//                                default:
//                                    throw new IllegalArgumentException("Unsupported type: " + type);
//                            }
//                        } else {
//                            pstmnt.setObject(++j, value);
//                        }
//                    }
//                    pstmnt.addBatch();
//                    batchCount++;
//                    if (batchCount / onePercent > progress) {
//                        progress++;
//                        if (progress % 10 == 0)
//                            log.info("{}% of data stored in table {}", progress, tableMetadata.getTableName());
//                    }
//                    if (batchCount % BATCHSIZE == 0) {
//                        pstmnt.executeBatch();
//                    }
//                }
//                if (batchCount > 0) {
//                    log.debug("Executing final batch of size {} for table {}", batchCount, tableMetadata.getTableName());
//                    pstmnt.executeBatch();
//                }
//                log.info("100% of data stored in table {}, all data stored", tableMetadata.getTableName());
//                conn.commit();
//            }
//        } catch (SQLException e) {
//            log.error("Failed to store data in table {}: {}", tableMetadata.getTableName(), e.getMessage());
//            throw e;
//        }
//    }


    private final DateTimeFormatter tsFormatter = DateTimeFormatter
            .ofPattern("yyyy-MM-dd HH:mm:ss.SSSX")
            .withZone(ZoneId.of("UTC"));

//    public void storeTable(TableMetadata tableMetadata, Map<String, List<Object>> generatedTableData) throws SQLException {
//        int totalRecords = tableMetadata.getRecordCount();
//        if (totalRecords == 0) return;
//
//        // Определяем размер чанка для каждого потока
//        int chunksCount = Math.min(THREAD_COUNT, (totalRecords / BATCHSIZE) + 1);
//        int recordsPerChunk = (int) Math.ceil((double) totalRecords / chunksCount);
//
//        ExecutorService executor = Executors.newFixedThreadPool(chunksCount);
//        List<Future<?>> futures = new ArrayList<>();
//
//        // Для общего прогресс-бара
//        AtomicInteger totalStored = new AtomicInteger(0);
//        int logStep = Math.max(1, totalRecords / 10); // Логируем каждые 10%
//
//        log.info("Starting parallel store for table {} using {} threads", tableMetadata.getTableName(), chunksCount);
//
//        for (int i = 0; i < chunksCount; i++) {
//            final int startIdx = i * recordsPerChunk;
//            final int endIdx = Math.min(startIdx + recordsPerChunk, totalRecords);
//
//            if (startIdx >= totalRecords) break;
//
//            futures.add(executor.submit(() -> {
//                try {
//                    processChunk(tableMetadata, generatedTableData, startIdx, endIdx, totalStored, logStep);
//                } catch (Exception e) {
//                    log.error("Error processing chunk {}-{} for table {}", startIdx, endIdx, tableMetadata.getTableName(), e);
//                    throw new RuntimeException(e);
//                }
//            }));
//        }
//
//
//
//        // Ждем завершения всех задач
//        try {
//            for (Future<?> future : futures) {
//                future.get();
//            }
//        } catch (InterruptedException | ExecutionException e) {
//            log.error("Parallel execution failed for table {}", tableMetadata.getTableName());
//            throw new SQLException("Error in parallel processing", e);
//        } finally {
//            executor.shutdown();
//        }
//
//        log.info("100% of data stored in table {}, total {} rows", tableMetadata.getTableName(), totalRecords);
//    }



    public void storeTable(TableMetadata tableMetadata, Map<String, List<Object>> generatedTableData) throws SQLException {
        int totalRecords = tableMetadata.getRecordCount();
        if (totalRecords == 0) return;

        int chunksCount = (int) Math.ceil((double) totalRecords / CHUNK_SIZE);
        ExecutorService executor = Executors.newFixedThreadPool(Math.min(THREAD_COUNT, chunksCount));
        List<Future<?>> futures = new ArrayList<>();

        AtomicInteger totalStored = new AtomicInteger(0);

        AtomicInteger lastReportedPercent = new AtomicInteger(-1);
        log.info("Starting COPY store for table {} using {} threads", tableMetadata.getTableName(), THREAD_COUNT);

        for (int i = 0; i < totalRecords; i += CHUNK_SIZE) {
            final int startIdx = i;
            final int endIdx = Math.min(startIdx + CHUNK_SIZE, totalRecords);

            futures.add(executor.submit(() -> {
                try {
                    processCopyChunk(tableMetadata, generatedTableData, startIdx, endIdx, totalStored, lastReportedPercent);
                } catch (Exception e) {
                    log.error("COPY Error in chunk {}-{}", startIdx, endIdx, e);
                    throw new RuntimeException(e);
                }
            }));
        }

        try {
            for (Future<?> future : futures) future.get();
        } catch (Exception e) {
            throw new SQLException("Parallel COPY failed", e);
        } finally {
            executor.shutdown();
        }
        log.info("Successfully copied {} rows into {}", totalRecords, tableMetadata.getTableName());
    }

    private void processCopyChunk(TableMetadata tableMetadata, Map<String, List<Object>> data,
                                  int start, int end, AtomicInteger counter, AtomicInteger lastReportedPercent) throws Exception {


        String columns = String.join(", ", tableMetadata.getColumns().keySet());
        String copySql = String.format("COPY %s (%s) FROM STDIN WITH (FORMAT CSV, HEADER FALSE, NULL 'NULL_MARKER')",
                tableMetadata.getNamespace() + "." + tableMetadata.getTableName(), columns);


        StringBuilder csvBuffer = new StringBuilder();
        List<String> colNames = new ArrayList<>(tableMetadata.getColumns().keySet());

        for (int i = start; i < end; i++) {
            for (int j = 0; j < colNames.size(); j++) {
                Object value = data.get(colNames.get(j)).get(i);
                ColumnMetadata meta = tableMetadata.getColumns().get(colNames.get(j));
                csvBuffer.append(formatForCsv(value, meta));

                if (j < colNames.size() - 1) csvBuffer.append(",");
            }
            csvBuffer.append("\n");
        }


        try (Connection conn = dataSource.getConnection()) {

            BaseConnection pgConn = conn.unwrap(BaseConnection.class);

            CopyManager copyManager = new CopyManager(pgConn);

            copyManager.copyIn(copySql, new StringReader(csvBuffer.toString()));

            int added = end - start;
            int total = counter.addAndGet(added);
            log.debug("Chunk {}-{} copied. Total: {}", start, end, total);
            reportCopyProgress(total, tableMetadata.getRecordCount(), tableMetadata.getTableName(), lastReportedPercent);
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