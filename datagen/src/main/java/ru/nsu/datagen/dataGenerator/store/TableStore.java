package ru.nsu.datagen.dataGenerator.store;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;

import java.io.File;
import java.io.FileWriter;
import java.sql.Array;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.SQLException;
import org.postgresql.util.PGobject;
import java.util.List;
import java.util.Map;

public class TableStore {
    private final Connection conn;

    public TableStore(Connection conn) {
        this.conn = conn;
    }

    public void storeTable(TableMetadata tableMetadata, Map<String, List<Object>> generatedTableData) throws SQLException {
        String queryString = getQueryString(tableMetadata);
        PreparedStatement pstmnt = conn.prepareStatement(queryString);
        // debug PK airplane code
        File log = new File("./log_govna.txt");
        FileWriter fw = null;
        try {
            fw = new FileWriter(log);
            log.createNewFile();

        } catch (Exception e) {
            System.out.println("Shti");
        }
        // debug PK airplane code end
        // Каждая строка
        for (int i = 0; i < tableMetadata.getRecordCount(); i++) {
            // Каждая колонка
            int j = 0;
            for (String columnName: tableMetadata.getColumns().keySet()) {
                Object value = generatedTableData.get(columnName).get(i);
                ColumnMetadata columnMetadata = tableMetadata.getColumns().get(columnName);
                String type = columnMetadata.getDataType();
                // debug PK airplane code

                if (columnMetadata.getName().equals("airplane_code")) {
                    try {
                        if (columnMetadata.isPrimaryKey()) { fw.write("PK : " + value.toString() + '\n'); }
                        else {fw.write("FK : " +value.toString() + '\n');}
                    } catch (Exception e) {
                        System.out.println(e.getMessage());
                    }
                }
                // debug PK airplane code end

                // Обработка массивов
                if (columnMetadata.getIsArray()) {
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
                    timeStr = "2000-01-01 " + timeStr;
                    PGobject pgObject = new PGobject();
                    pgObject.setType("timestamp");
                    pgObject.setValue(timeStr);
                    pstmnt.setObject(++j, pgObject);
                } else if ((type.equals("timestamp with time zone") || type.equals("timestamptz")) && value instanceof String) {
                    // Обработка timestamp with time zone
                    String timeStr = (String) value;
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
                } else if (type.contains("num")) {
                    pstmnt.setObject(++j, value);
                } else if (value instanceof String && !type.contains("char")) {
                    PGobject pgObject = new PGobject();
                    pgObject.setType(type);
                    pgObject.setValue((String) value);
                    pstmnt.setObject(++j, pgObject);
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
            if (i++ < tableMetadata.getColumns().keySet().size()) {
                stringBuilder.append(", ");
            }
        }
        stringBuilder.append(") VALUES (");
        for (i = 1; i <= tableMetadata.getColumns().keySet().size(); i++) {
            stringBuilder.append("?");
            if (i < tableMetadata.getColumns().keySet().size()) {
                stringBuilder.append(", ");
            }
        }
        stringBuilder.append(")");

        return stringBuilder.toString();
    }
}