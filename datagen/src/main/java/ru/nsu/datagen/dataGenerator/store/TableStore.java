package ru.nsu.datagen.dataGenerator.store;

import com.zaxxer.hikari.HikariDataSource;
import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;

import java.math.BigDecimal;
import java.sql.*;

import org.postgresql.util.PGobject;
import java.util.List;
import java.util.Map;

@Slf4j
public class TableStore {
    private final HikariDataSource dataSource;
    private final int batchSize = 1000;

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
                for (int i = 0; i < tableMetadata.getRecordCount(); i++) {
                    // Каждая колонка
                    int j = 0;
                    for (String columnName : tableMetadata.getColumns().keySet()) {
                        Object value = generatedTableData.get(columnName).get(i);
                        ColumnMetadata columnMetadata = tableMetadata.getColumns().get(columnName);
                        String type = columnMetadata.getDataType();

                        // Обработка массивов
                        if (columnMetadata.isArray()) {
                            if (type.contains("[]")) {
                                type = type.substring(0, type.indexOf("[]"));
                            }
                            Array sqlArray = conn.createArrayOf(type, new Object[]{value});
                            pstmnt.setArray(++j, sqlArray);
                        } else if (type.equals("interval")) {
                            PGobject pgObject = new PGobject();
                            pgObject.setType("interval");
                            pgObject.setValue((String) value);
                            pstmnt.setObject(++j, pgObject);
                        } else if (type.equals("timestamp without time zone") || type.equals("time without time zone")) {
                            String timeStr = (String) value;
                            PGobject pgObject = new PGobject();
                            pgObject.setType("timestamp");
                            pgObject.setValue(timeStr);
                            pstmnt.setObject(++j, pgObject);
                        } else if ((type.equals("timestamp with time zone") || type.equals("timestamptz")) && value instanceof String timeStr) {
                            if (timeStr.contains("[")) {
                                timeStr = timeStr.substring(0, timeStr.indexOf("["));
                            }

                            if (!timeStr.contains("T") && !timeStr.contains("-")) {
                                timeStr = "2000-01-01 " + timeStr;
                            }

                            PGobject pgObject = new PGobject();
                            pgObject.setType("timestamptz");
                            pgObject.setValue(timeStr);
                            pstmnt.setObject(++j, pgObject);
                        } else if (type.contains("time zone")) {
                            PGobject pgObject = new PGobject();
                            pgObject.setType("timestamp");
                            pgObject.setValue((String) value);
                            pstmnt.setObject(++j, pgObject);
                        } else if (type.contains("tstzrange")) {
                            PGobject pgObject = new PGobject();
                            pgObject.setType("tstzrange");
                            pgObject.setValue((String) value);
                            pstmnt.setObject(++j, pgObject);
                        } else if (type.contains("date")) {
                            PGobject pgObject = new PGobject();
                            pgObject.setType("date");
                            pgObject.setValue(value.toString());
                            pstmnt.setObject(++j, pgObject);
                        } else if (type.contains("num")) {
                            pstmnt.setObject(++j, value);
                        } else if (type.contains("money")) {
                            PGobject pgObject = new PGobject();
                            pgObject.setType("money");
                            pgObject.setValue(value.toString());
                            pstmnt.setObject(++j, pgObject);
                        } else if (value instanceof String && !type.contains("char")) {
                            switch (type.toLowerCase()) {
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
                        pstmnt.executeBatch();
                        batchCount = 0;
                    }
                }
                if (batchCount > 0) {
                    log.debug("Executing final batch of size {} for table {}", batchCount, tableMetadata.getTableName());
                    pstmnt.executeBatch();
                }
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