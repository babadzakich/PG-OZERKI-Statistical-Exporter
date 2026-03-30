package ru.nsu.datagen.dataGenerator.store;

import com.zaxxer.hikari.HikariDataSource;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;

import java.math.BigDecimal;
import java.sql.*;
import java.util.List;
import java.util.Map;

import org.postgresql.util.PGobject;

@Slf4j
public class TableStore {
    private final HikariDataSource dataSource;
    private final int batchSize = 10000;

    public TableStore(HikariDataSource dataSource) {
        this.dataSource = dataSource;
    }

    public void storeTable(TableMetadata tableMetadata, Map<String, List<Object>> generatedTableData) throws SQLException {
        String queryString = getQueryString(tableMetadata);
        try (Connection conn = dataSource.getConnection()) {
            try (Statement statement = conn.createStatement()) {
                statement.execute("SET search_path TO public, bookings");
            }
            try (PreparedStatement pstmnt = conn.prepareStatement(queryString)) {
                int batchCount = 0;
                // Каждая строка
                conn.setAutoCommit(false);
                for (int i = 0; i < tableMetadata.getRecordCount(); i++) {
                    // Каждая колонка
                    int j = 0;
                    for (String columnName : tableMetadata.getColumns().keySet()) {
                        Object value = generatedTableData.get(columnName).get(i);
                        ColumnMetadata columnMetadata = tableMetadata.getColumns().get(columnName);
                        String type = columnMetadata.getDataType();
                        String normalizedType = type.toLowerCase();

                        // Обработка массивов
                        if (columnMetadata.isArray()) {
                            if (type.contains("[]")) {
                                type = type.substring(0, type.indexOf("[]"));
                            }
                            Array sqlArray = conn.createArrayOf(type, new Object[]{value});
                            pstmnt.setArray(++j, sqlArray);
                        } else if (normalizedType.equals("interval")) {
                            PGobject pgObject = new PGobject();
                            pgObject.setType("interval");
                            pgObject.setValue(value.toString());
                            pstmnt.setObject(++j, pgObject);
                        } else if (normalizedType.equals("time without time zone") || normalizedType.equals("time")) {
                            pstmnt.setObject(++j, value, Types.TIME);
                        } else if (normalizedType.equals("timestamp without time zone") || normalizedType.equals("timestamp")) {
                            if (value instanceof java.time.Instant instant) {
                                pstmnt.setTimestamp(++j, Timestamp.from(instant));
                            } else {
                                pstmnt.setObject(++j, value, Types.TIMESTAMP);
                            }
                        } else if (normalizedType.equals("timestamp with time zone") || normalizedType.equals("timestamptz")) {
                            if (value instanceof java.time.Instant instant) {
                                pstmnt.setObject(++j, instant, Types.TIMESTAMP_WITH_TIMEZONE);
                            } else if (value instanceof String s) {
                                PGobject pgObject = new PGobject();
                                pgObject.setType("timestamptz");
                                pgObject.setValue(s);
                                pstmnt.setObject(++j, pgObject);
                            } else {
                                pstmnt.setObject(++j, value);
                            }
                        } else if (normalizedType.contains("tstzrange")) {
                            PGobject pgObject = new PGobject();
                            pgObject.setType("tstzrange");
                            pgObject.setValue((String) value);
                            pstmnt.setObject(++j, pgObject);
                        } else if (normalizedType.contains("date")) {
                            PGobject pgObject = new PGobject();
                            pgObject.setType("date");
                            pgObject.setValue(value.toString());
                            pstmnt.setObject(++j, pgObject);
                        } else if (normalizedType.contains("num")) {
                            pstmnt.setObject(++j, value);
                        } else if (normalizedType.contains("money")) {
                            PGobject pgObject = new PGobject();
                            pgObject.setType("money");
                            pgObject.setValue(value.toString());
                            pstmnt.setObject(++j, pgObject);
                        } else if (value instanceof String && !normalizedType.contains("char")) {
                            switch (normalizedType) {
                                case "smallint":
                                case "int2":
                                    pstmnt.setShort(++j, Short.parseShort((String) value));
                                    break;
                                case "integer":
                                case "int4":
                                    pstmnt.setInt(++j, Integer.parseInt((String) value));
                                    break;
                                case "bigint":
                                case "int8":
                                    pstmnt.setLong(++j, Long.parseLong((String) value));
                                    break;
                                case "varchar":
                                case "text":
                                    pstmnt.setString(++j, (String) value);
                                    break;
                                case "boolean":
                                case "bool":
                                    pstmnt.setBoolean(++j, Boolean.parseBoolean((String) value));
                                    break;
                                case "decimal":
                                case "numeric":
                                    pstmnt.setBigDecimal(++j, new BigDecimal((String) value));
                                    break;
                                case "float8":
                                case "double":
                                case "double precision":
                                    pstmnt.setDouble(++j, Double.parseDouble((String) value));
                                    break;
                                case "float4":
                                case "real":
                                    pstmnt.setFloat(++j, Float.parseFloat((String) value));
                                    break;
                                case "bytea":
                                    pstmnt.setBytes(++j, ((String) value).getBytes());
                                    break;
                                default:
                                    throw new IllegalArgumentException("Unsupported type: " + type);
                            }
                        } else {
                            pstmnt.setObject(++j, value);
                        }
                    }
                    pstmnt.addBatch();
                    batchCount++;
                    if (batchSize == batchCount) {
//                        log.debug("Executing batch for table {}", tableMetadata.getNamespace() + "." + tableMetadata.getTableName());
                        pstmnt.executeBatch();
                        batchCount = 0;
                    }
                }
                if (batchCount > 0) {
                    log.debug("Executing final batch of size {} for table {}", batchCount, tableMetadata.getTableName());
                    pstmnt.executeBatch();
                }
                log.info("Finished storing data in table {}", tableMetadata.getTableName());
                conn.commit();
            }
        } catch (SQLException e) {
            log.error("Failed to store data in table {}: {}", tableMetadata.getTableName(), e.getMessage());
            throw e;
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
}