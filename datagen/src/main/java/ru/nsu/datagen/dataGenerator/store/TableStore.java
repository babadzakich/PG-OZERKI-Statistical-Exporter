package ru.nsu.datagen.dataGenerator.store;

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
    private final Connection conn;

    public TableStore(Connection conn) {
        this.conn = conn;
    }

    public void storeTable(TableMetadata tableMetadata, Map<String, List<Object>> generatedTableData) throws SQLException{
        String queryString = getQueryString(tableMetadata);
        conn.createStatement().execute("SET search_path TO public, bookings");
        PreparedStatement pstmnt = conn.prepareStatement(queryString);

        // Каждая строка
        for (int i = 0; i < tableMetadata.getRecordCount(); i++) {
            // Каждая колонка
            int j = 0;
            for (String columnName: tableMetadata.getColumns().keySet()) {
                Object value = generatedTableData.get(columnName).get(i);
                ColumnMetadata columnMetadata = tableMetadata.getColumns().get(columnName);
                String type = columnMetadata.getDataType();

                // Обработка массивов
                if (columnMetadata.isArray()) {
                    //System.out.println(type);
                    if (type.contains("[]")) {
                        type = type.substring(0, type.indexOf("[]"));
                    }
                    Array sqlArray = conn.createArrayOf(type, new Object[]{value});
                    pstmnt.setArray(++j, sqlArray);
                    // fuck
                } else if (type.equals("interval")) {
                    // Обработка интервала
                    PGobject pgObject = new PGobject();
                    pgObject.setType("interval");
                    pgObject.setValue((String) value);
                    pstmnt.setObject(++j, pgObject);
                } else if (type.equals("timestamp without time zone") || type.equals("time without time zone")) {
                    // СУПЕРНАДЕЖНОЕ РЕШЕНИЕ ДЛЯ TIMESTAMP
                    String timeStr = (String) value;
                    // УПРОЩАЕМ: всегда добавляем дату 2000-01-01 к любому времени
                    // Это абсолютно гарантированно сработает
//                    timeStr = "2000-01-01 " + timeStr;
                    PGobject pgObject = new PGobject();
                    pgObject.setType("timestamp");
                    pgObject.setValue(timeStr);
                    pstmnt.setObject(++j, pgObject);
                } else if ((type.equals("timestamp with time zone") || type.equals("timestamptz")) && value instanceof String timeStr) {
                    // Обработка timestamp with time zone

                    // Удаляем [Zone] из формата ZonedDateTime (например, [Etc/GMT-9])
                    if (timeStr.contains("[")) {
                        timeStr = timeStr.substring(0, timeStr.indexOf("["));
                    }

                    // Для timestamptz тоже может быть только время
                    if (!timeStr.contains("T") && !timeStr.contains("-")) {
                        // Если нет T и нет дефиса, значит только время
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
                } else if(type.contains("tstzrange")) {
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
                    PGobject pgObject = new PGobject();
                    pgObject.setType(type);
                    pgObject.setValue((String) value);
                    //pstmnt.setObject(++j, value);
                    switch(type.toLowerCase()) {
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
                            pstmnt.setString(++j, (String)value);
                            break;
                        case "boolean":
                        case "bool":
                            pstmnt.setBoolean(++j, Boolean.parseBoolean((String)value));
                            break;
                        case "decimal":
                        case "numeric":
                            pstmnt.setBigDecimal(++j, new BigDecimal((String) value));
                            break;
                        case "float8":
                        case "double":
                        case "double precision":
                            pstmnt.setDouble(++j, Double.parseDouble((String)value));
                            break;
                        case "float4":
                        case "real":
                            pstmnt.setFloat(++j, Float.parseFloat((String)value));
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
        }
        pstmnt.executeBatch();
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