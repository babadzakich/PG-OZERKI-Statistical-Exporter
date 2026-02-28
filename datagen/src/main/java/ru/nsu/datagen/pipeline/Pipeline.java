package ru.nsu.datagen.pipeline;

import java.sql.SQLException;
import java.util.List;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
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
        HikariConfig hikariConfig = new HikariConfig();
        hikariConfig.setJdbcUrl("jdbc:postgresql://" + host + ":" + port + "/" + dbname + "?currentSchema=bookings");
        hikariConfig.setUsername(user);
        hikariConfig.setPassword(password);

        try (HikariDataSource dataSource = new HikariDataSource(hikariConfig)) {
            List<TableMetadata> rawImportedData = Importer.startImport(schemaScriptPath, statisticData, dataSource.getConnection());
            DatabaseDataGenerator.generateData(rawImportedData, dataSource);
        } catch (Exception e) {
            log.error("Pipeline failed: ", e);
            throw e;
        }
    }

}
