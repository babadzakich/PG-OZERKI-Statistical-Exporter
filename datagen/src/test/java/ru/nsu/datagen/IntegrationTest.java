package ru.nsu.datagen;

import java.io.IOException;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import org.junit.jupiter.api.*;
import org.testcontainers.containers.PostgreSQLContainer;
import org.testcontainers.junit.jupiter.Container;
import org.testcontainers.junit.jupiter.Testcontainers;

import ru.nsu.datagen.dataGenerator.DatabaseDataGenerator;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.importer.Importer;

/**
 * Интеграционный тест для проверки всего pipeline генерации
 * данных.
 * Использует локальную PostgreSQL БД.
 * Перед запуском теста убедитесь что БД доступна
 */
@Testcontainers
public class IntegrationTest {

    @Container
    private static final PostgreSQLContainer<?> postgres =
            new PostgreSQLContainer<>("postgres:16");

    private static HikariDataSource dataSource;
    private Config config;

    @BeforeAll
    static void beforeAll() throws SQLException {
        HikariConfig hikariConfig = new HikariConfig();
        hikariConfig.setJdbcUrl(postgres.getJdbcUrl());
        hikariConfig.setUsername(postgres.getUsername());
        hikariConfig.setPassword(postgres.getPassword());
        dataSource = new HikariDataSource(hikariConfig);
        try (Connection conn = dataSource.getConnection();
             Statement stmt = conn.createStatement()) {
            stmt.execute("CREATE DATABASE \"testdb\"");
        }
    }

    @BeforeEach
    void setUp(TestInfo testInfo) throws IOException, SQLException {
        String configPath = testInfo.getTestMethod()
                .map(m -> m.getAnnotation(ConfigFile.class))
                .map(ConfigFile::value)
                .orElse("config.yaml");
        config = new Config(configPath);
        try (Connection conn = dataSource.getConnection()) {
            // Очищаем БД перед тестом
            try (Statement stmt = conn.createStatement()) {
                for (var dbHolder : config.getTables()) {
                    stmt.execute("DROP TABLE IF EXISTS " + dbHolder.getName() + " CASCADE");
                }
            }
        }
    }

    @AfterAll
    static void tearDown() {
        if (dataSource != null && !dataSource.isClosed()) {
            dataSource.close();
        }
    }

    /**
     * Основной регрессионный тест, проверяющий работу всего pipeline:
     * 1. Создание схемы БД
     * 2. Импорт статистики
     * 3. Генерация данных
     * 4. Проверка целостности данных и ограничений
     */
    @Test
    void testFullPipelineIntegration() throws Exception {
        ClassLoader classLoader = getClass().getClassLoader();
        String schemaPath = Paths.get(classLoader.getResource(config.getSCHEMA_PATH()).toURI()).toString();
        String statsPath = Paths.get(classLoader.getResource(config.getSTATS_PATH()).toURI()).toString();

        try (Connection conn = dataSource.getConnection()) {
            // Импорт схемы и статистики
            List<TableMetadata> importedData = Importer.startImport(schemaPath, statsPath, conn);
            assertNotNull(importedData, "Импортированные данные не должны быть null");
            assertFalse(importedData.isEmpty(), "Импортированные данные не должны быть пустыми");
            System.out.println("✓ Импорт схемы и статистики выполнен успешно");

            // Генерация данных
            DatabaseDataGenerator.generateData(importedData, dataSource);
            System.out.println("✓ Генерация данных завершена");

            // Проверка целостности данных и ограничений
            for (var dbHolder : config.getTables()) {
                checkTableIntegrity(conn, dbHolder);
            }

            System.out.println("\n========================================");
            System.out.println("✓✓✓ ВСЕ ПРОВЕРКИ ПРОЙДЕНЫ УСПЕШНО ✓✓✓");
            System.out.println("========================================");
        }
    }

    /**
     * Проверяет целостность таблицы: размер, уникальность PK и UNIQUE
     */
    private void checkTableIntegrity(Connection conn, TableHolder dbHolder) throws SQLException {
        checkTableSize(conn, dbHolder);
        
        validateUniqueConstraints(conn, dbHolder);

        validatePrimaryKeys(conn, dbHolder);

        System.out.println("✓ Проверка целостности таблицы " + dbHolder.getName() + " завершена\n");

    }

    /**
     * Проверяет что все первичные ключи уникальны и положительны
     */
    private void validatePrimaryKeys(Connection conn, TableHolder dbHolder) throws SQLException {
        if (dbHolder.getPks() == null || dbHolder.getPks().isEmpty()) {
            return; // Нет PK для проверки
        }
        for (var pkCol : dbHolder.getPks()) {
            try (Statement stmt = conn.createStatement()) {
                ResultSet rs = stmt.executeQuery(
                "SELECT COUNT(DISTINCT " + pkCol + ") as distinct_count, COUNT(*) as total_count " +
                "FROM " + dbHolder.getSchema() + "." + dbHolder.getName());
                rs.next();
                int distinctCount = rs.getInt("distinct_count");
                int totalCount = rs.getInt("total_count");
                assertEquals(totalCount, distinctCount,
                "Колонка " + pkCol + " должна содержать уникальные значения (PK)");
                System.out.println("✓ Первичный ключ " + pkCol + " уникален");
            }

            try (Statement stmt = conn.createStatement()) {
                ResultSet rs = stmt.executeQuery(
                        "SELECT COUNT(*) FROM " + dbHolder.getSchema() + "." + dbHolder.getName() 
                        + " WHERE " + pkCol + " <= 0");
                rs.next();
                int invalidIds = rs.getInt(1);
                assertEquals(0, invalidIds, "Все " + pkCol + " должны быть положительными");
                System.out.println("✓ Все значения первичного ключа " + pkCol + " положительны");
            }
        }
    }

    /**
     * Проверяет что все уникальные ограничения соблюдены
     */
    private void validateUniqueConstraints(Connection conn, TableHolder dbHolder) throws SQLException {
        if (dbHolder.getUniques() == null || dbHolder.getUniques().isEmpty()) {
            return; // Нет уникальных ограничений для проверки
        }
        for (var uniqueCols : dbHolder.getUniques()) {
            String colsJoined = String.join(", ", uniqueCols);
            try (Statement stmt = conn.createStatement()) {
                ResultSet rs = stmt.executeQuery(
                "SELECT COUNT(*) as duplicate_count FROM (" +
                        "  SELECT " + colsJoined + ", COUNT(*) as cnt " +
                        "  FROM " + dbHolder.getSchema() + "." + dbHolder.getName() + " " +
                        "  GROUP BY " + colsJoined + " " +
                        "  HAVING COUNT(*) > 1" +
                        ") duplicates");
                rs.next();
                long duplicateCount = rs.getLong("duplicate_count");
                assertEquals(0, duplicateCount,
                "Пары (" + colsJoined + ") должны быть уникальными");
                System.out.println("✓ UNIQUE constraint (" + colsJoined + ") соблюден");
            }
        }
    }

    // Проверяет что размер таблицы соответствует ожидаемому
    private void checkTableSize(Connection conn, TableHolder dbHolder) throws SQLException {
        // Проверка количества записей
        try (Statement stmt = conn.createStatement()) {
            ResultSet rs = stmt.executeQuery(
                    "SELECT COUNT(*) FROM " + dbHolder.getSchema() + "." + dbHolder.getName());
            rs.next();
            long rowCount = rs.getLong(1);
            assertEquals(dbHolder.getSize(), rowCount,
                    "Количество записей в таблице " + dbHolder.getName() +
                            " должно быть " + dbHolder.getSize());
            System.out.println("✓ Таблица "  + dbHolder.getSchema() + "." + dbHolder.getName() + ": " + rowCount + " записей");
        }
    }
}
