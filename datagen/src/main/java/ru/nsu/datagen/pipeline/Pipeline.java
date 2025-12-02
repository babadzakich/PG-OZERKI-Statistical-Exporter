package ru.nsu.datagen.pipeline;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.List;

import ru.nsu.datagen.dataGenerator.DatabaseDataGenerator;
import ru.nsu.datagen.importer.Importer;

public class Pipeline {
    static public void startPipeline(
            String host, Integer port, String dbname, String user, String password,
            String schemaScriptPath, String statisticData
    ) throws SQLException {
        String url = "jdbc:postgresql://" + host + ":" + port + "/" + dbname + "?currentSchema=bookings";
        try (Connection conn = DriverManager.getConnection(url, user, password)) {
            List<String[]> rawImportedData = Importer.startImport(schemaScriptPath, statisticData, conn);
            DatabaseDataGenerator.generateData(rawImportedData, conn);
        } catch (Exception e) {
            System.err.println(e);
            throw e;
        }
    }

}
