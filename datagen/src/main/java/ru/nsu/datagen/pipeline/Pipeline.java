package ru.nsu.datagen.pipeline;

import java.sql.SQLException;
import java.util.Map;

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
        hikariConfig.setJdbcUrl("jdbc:postgresql://" + host + ":" + port + "/" + dbname + "?currentSchema=bookings&reWriteBatchedInserts=true");
        hikariConfig.setUsername(user);
        hikariConfig.setPassword(password);

        // Connection pool configuration for optimal performance
        hikariConfig.setMaximumPoolSize(20);
        hikariConfig.setMinimumIdle(5);
        hikariConfig.setConnectionTimeout(30000);
        hikariConfig.setIdleTimeout(600000);
        hikariConfig.setMaxLifetime(1800000);

        // Позволяет драйверу кэшировать распарсенные SQL-запросы
        hikariConfig.addDataSourceProperty("cachePrepStmts", "true");
        // Лимит на количество кэшируемых запросов
        hikariConfig.addDataSourceProperty("prepStmtCacheSize", "250");
        // Лимит на длину запроса, который можно закэшировать
        hikariConfig.addDataSourceProperty("prepStmtCacheSqlLimit", "2048");

        try (HikariDataSource dataSource = new HikariDataSource(hikariConfig)) {
            Map<String, TableMetadata> rawImportedData = Importer.startImport(schemaScriptPath, statisticData, dataSource.getConnection());
            DatabaseDataGenerator.generateData(rawImportedData, dataSource);
        } catch (Exception e) {
            log.error("Pipeline failed: ", e);
            throw e;
        }
    }

}
