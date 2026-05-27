package ru.nsu.datagen.pipeline;

import java.io.IOException;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ExecutorService;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.argValidation.Arguments;
import ru.nsu.datagen.dataGenerator.DatabaseDataGenerator;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.importer.Importer;

/**
 * Оркестрирует весь пайплайн генерации данных.
 *
 * <p>Последовательность шагов:
 * <ol>
 *   <li>Создаёт пул соединений HikariCP и пул потоков генерации.</li>
 *   <li>Через {@link ru.nsu.datagen.importer.Importer} применяет schema.sql к БД и парсит CSV-статистику.</li>
 *   <li>Запускает {@link ru.nsu.datagen.dataGenerator.DatabaseDataGenerator} для генерации и вставки данных.</li>
 *   <li>Опционально выполняет indexes.sql.</li>
 *   <li>Вызывает {@code pg_reload_conf()} и {@code ANALYZE} для актуализации планировщика.</li>
 * </ol>
 *
 * <p><b>Внимание:</b> URL соединения жёстко добавляет {@code currentSchema=bookings}.
 * При использовании другой схемы это нужно изменить здесь.
 */
@Slf4j
public class Pipeline {
    /**
     * Запускает полный пайплайн генерации данных на основе переданных аргументов.
     *
     * @param args разобранные и провалидированные аргументы запуска
     * @throws SQLException при ошибке работы с БД
     * @throws IOException  при ошибке чтения входных файлов
     */
    static public void startPipeline(Arguments args) throws SQLException, IOException {
        HikariConfig hikariConfig = new HikariConfig();
        hikariConfig.setJdbcUrl("jdbc:postgresql://" + args.host + ":" + args.port + "/" + args.dbname + "?currentSchema=bookings&reWriteBatchedInserts=true");
        hikariConfig.setUsername(args.user);
        hikariConfig.setPassword(args.password);

        // Connection pool configuration for optimal performance
        hikariConfig.setMaximumPoolSize(args.generationThreadPoolSize + args.tableStoreThreads * args.globStoreThreads);
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

        try(ExecutorService generationExecutor = java.util.concurrent.Executors.newFixedThreadPool(args.generationThreadPoolSize)) {

            try (HikariDataSource dataSource = new HikariDataSource(hikariConfig)) {
                Map<String, TableMetadata> rawImportedData = Importer.startImport(args.schemaPath, args.statPath, args.constraintFile, dataSource.getConnection());
                DatabaseDataGenerator.generateData(rawImportedData, dataSource, generationExecutor, args.batchSize, args.globStoreThreads, args.tableStoreThreads);
                Optional.ofNullable(args.indexFile).ifPresent(path -> {
                    try {
                        Importer.importSchemas(path, dataSource.getConnection().createStatement());
                    } catch (SQLException e) {
                        log.error("Failed to import indexes from SQL file.", e);
                        throw new RuntimeException(e);
                    }
                });

                try (Connection conn = dataSource.getConnection();
                     Statement stmt = conn.createStatement();
                     ResultSet rs = stmt.executeQuery("SELECT pg_reload_conf()")) {

                    if (rs.next()) {
                        log.info("Config reloaded {}", rs.getBoolean(1));
                    }
                }

                try (Connection conn = dataSource.getConnection();
                     Statement stmt = conn.createStatement()
                ) {
                    stmt.execute("ANALYZE");
                }
            } catch (Exception e) {
                log.error("Pipeline failed: ", e);
                throw e;
            }
        }
    }

}
