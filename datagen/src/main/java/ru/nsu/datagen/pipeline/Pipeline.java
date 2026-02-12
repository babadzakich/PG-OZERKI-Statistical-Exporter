package ru.nsu.datagen.pipeline;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.util.List;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.DatabaseDataGenerator;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.importer.Importer;

@Slf4j
public class Pipeline {
    static public void startPipeline(
            String host, Integer port, String dbname, String user, String password,
            String schemaScriptPath, String statisticData
    ) throws SQLException {
        String url = "jdbc:postgresql://" + host + ":" + port + "/" + dbname + "?currentSchema=bookings";
        try (Connection conn = DriverManager.getConnection(url, user, password)) {
            List<TableMetadata> rawImportedData = Importer.startImport(schemaScriptPath, statisticData, conn);
            DatabaseDataGenerator.generateData(rawImportedData, conn);
        } catch (Exception e) {
            log.error("Pipeline failed: ", e);
            throw e;
        }
    }

}
