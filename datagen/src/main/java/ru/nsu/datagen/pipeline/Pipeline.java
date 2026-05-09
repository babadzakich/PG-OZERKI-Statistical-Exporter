package ru.nsu.datagen.pipeline;

import java.io.IOException;
import java.sql.SQLException;
import java.util.Map;
import java.util.concurrent.ExecutorService;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.argValidation.Arguments;
import ru.nsu.datagen.dataGenerator.DatabaseDataGenerator;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.importer.Importer;

@Slf4j
public class Pipeline {
    static public void startPipeline(Arguments args) throws SQLException, IOException {
        HikariConfig hikariConfig = new HikariConfig();
        hikariConfig.setJdbcUrl("jdbc:postgresql://" + args.host + ":" + args.port + "/" + args.dbname + "?currentSchema=bookings&reWriteBatchedInserts=true");
        hikariConfig.setUsername(args.user);
        hikariConfig.setPassword(args.password);

        // Connection pool configuration for optimal performance
        hikariConfig.setMaximumPoolSize(args.globStoreThreads * args.tableStoreThreads);
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

        ExecutorService generationExecutor = java.util.concurrent.Executors.newFixedThreadPool(args.generationThreadPoolSize);

        try (HikariDataSource dataSource = new HikariDataSource(hikariConfig)) {
            Map<String, TableMetadata> rawImportedData = Importer.startImport(args.schemaPath, args.statPath, args.constraintFile, dataSource.getConnection());
            DatabaseDataGenerator.generateData(rawImportedData, dataSource, generationExecutor, args.batchSize, args.globStoreThreads, args.tableStoreThreads);
        } catch (Exception e) {
            log.error("Pipeline failed: ", e);
            throw e;
        }
    }

}
