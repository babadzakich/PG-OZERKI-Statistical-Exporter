package ru.nsu.datagen;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import org.junit.jupiter.api.AfterAll;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestInfo;
import org.testcontainers.containers.PostgreSQLContainer;
import org.testcontainers.junit.jupiter.Container;
import org.testcontainers.junit.jupiter.Testcontainers;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.DatabaseDataGenerator;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.importer.Importer;
import ru.nsu.datagen.plancheck.struct.PlanTree;
import ru.nsu.datagen.plancheck.ted.TreeEditDistance;

/**
 * Интеграционный тест для проверки всего pipeline генерации
 * данных.
 * Использует локальную PostgreSQL БД.
 * Перед запуском теста убедитесь что БД доступна
 */
@Slf4j
@Testcontainers
public class IntegrationTest {

    @Container
    private static final PostgreSQLContainer<?> postgres =
            new PostgreSQLContainer<>("postgres:17");

    private static HikariDataSource dataSource;
    private Config config;

    @BeforeAll
    static void beforeAll() throws SQLException {
        try {
            System.setProperty("api.version", "1.44");
            HikariConfig hikariConfig = new HikariConfig();
            hikariConfig.setJdbcUrl(postgres.getJdbcUrl());
            hikariConfig.setUsername(postgres.getUsername());
            hikariConfig.setPassword(postgres.getPassword());
            dataSource = new HikariDataSource(hikariConfig);
            try (Connection conn = dataSource.getConnection();
                 Statement stmt = conn.createStatement()) {
                stmt.execute("CREATE DATABASE \"testdb\"");
            }
        } catch (SQLException e) {
            log.error("Failed to set up database connection", e);
            throw e;
        }
    }

    @BeforeEach
    void setUp(TestInfo testInfo) throws IOException, SQLException {
        String configPath = testInfo.getTestMethod()
                .map(m -> m.getAnnotation(ConfigFile.class))
                .map(ConfigFile::value)
                .orElse("Base/config.yaml");
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
    @ConfigFile("big/big.yaml")
    void testFullPipelineIntegration() throws Exception {
        ClassLoader classLoader = getClass().getClassLoader();
        String schemaPath = Paths.get(classLoader.getResource(config.getSCHEMA_PATH()).toURI()).toString();
        String statsPath = Paths.get(classLoader.getResource(config.getSTATS_PATH()).toURI()).toString();

        try (Connection conn = dataSource.getConnection()) {
            // Импорт схемы и статистики
            Map<String, TableMetadata> importedData = Importer.startImport(schemaPath, statsPath, conn);
            assertNotNull(importedData, "Импортированные данные не должны быть null");
            assertFalse(importedData.isEmpty(), "Импортированные данные не должны быть пустыми");
            System.out.println("✓ Импорт схемы и статистики выполнен успешно");
            ExecutorService executorService = Executors.newFixedThreadPool(Runtime.getRuntime().availableProcessors());
            // Генерация данных
            DatabaseDataGenerator.generateData(importedData, dataSource, executorService, 1000);
            System.out.println("✓ Генерация данных завершена");

            // Проверка целостности данных и ограничений
            for (var dbHolder : config.getTables()) {
                checkTableIntegrity(conn, dbHolder);
            }

            // Сравнение планов запросов
            compareQueryPlans(conn, config);

            System.out.println("\n========================================");
            System.out.println("✓✓✓ ВСЕ ПРОВЕРКИ ПРОЙДЕНЫ УСПЕШНО ✓✓✓");
            System.out.println("========================================");
        }
    }

    /**
     * Выполняет EXPLAIN (ANALYZE, FORMAT JSON) для запроса из конфига
     * и сравнивает полученный план с эталонным из файла.
     */
    private void compareQueryPlans(Connection conn, Config config) throws Exception {
        String queryPath = config.getQueryPath();
        String sourcePlanPath = config.getSourcePlanPath();
        if (queryPath == null || sourcePlanPath == null) {
            throw new Exception("null path to query or source_plan");
        }

        String sqlQuery = readResourceFile(queryPath);
        String expectedPlanJson = readResourceFile(sourcePlanPath);

        String explainJson = executeExplainAnalyze(conn, sqlQuery);

        PlanTree actualTree = PlanTree.fromJson(explainJson);
        PlanTree expectedTree = PlanTree.fromJson(expectedPlanJson);

        // Вычисление расстояния редактирования деревьев
        float similarity = TreeEditDistance.computeSimilarity(actualTree.root, expectedTree.root);

        System.out.println("Совпадение плано на " + similarity + "%");
        // Проверяем совпадения на >50%
        assertTrue(similarity > 50, "Similarity gt 50%");

    }

    /**
     * Выполняет EXPLAIN (ANALYZE, FORMAT JSON) для заданного SQL-запроса.
     */
    private String executeExplainAnalyze(Connection conn, String sql) throws SQLException {
        String explainSql = "EXPLAIN (VERBOSE, FORMAT JSON) " + sql;
        try (Statement stmt = conn.createStatement()
             ) {
            stmt.execute("ANALYZE");
            ResultSet rs = stmt.executeQuery(explainSql);
            rs.next();
            // PostgreSQL возвращает JSON в первой колонке первой строки
            log.info("Query plan in new database: {}", rs.getString(1));
            return rs.getString(1);
        }
    }

    /**
     * Читает содержимое файла из ресурсов как строку.
     */
    private String readResourceFile(String resourcePath) throws IOException {
        ClassLoader classLoader = getClass().getClassLoader();
        try (InputStream is = classLoader.getResourceAsStream(resourcePath)) {
            if (is == null) {
                throw new IOException("Resource not found: " + resourcePath);
            }
            return new String(is.readAllBytes(), StandardCharsets.UTF_8);
        }
    }

    private void checkTableIntegrity(Connection conn, TableHolder dbHolder) throws SQLException {
        checkTableSize(conn, dbHolder);
        validateUniqueConstraints(conn, dbHolder);
        validatePrimaryKeys(conn, dbHolder);
        System.out.println("✓ Проверка целостности таблицы " + dbHolder.getName() + " завершена\n");
    }

    private void validatePrimaryKeys(Connection conn, TableHolder dbHolder) throws SQLException {
        if (dbHolder.getPks() == null || dbHolder.getPks().isEmpty()) {
            return;
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

//            try (Statement stmt = conn.createStatement()) {
//                ResultSet rs = stmt.executeQuery(
//                        "SELECT COUNT(*) FROM " + dbHolder.getSchema() + "." + dbHolder.getName()
//                                + " WHERE " + pkCol + " <= 0");
//                rs.next();
//                int invalidIds = rs.getInt(1);
//                assertEquals(0, invalidIds, "Все " + pkCol + " должны быть положительными");
//                System.out.println("✓ Все значения первичного ключа " + pkCol + " положительны");
//            }
        }
    }

    private void validateUniqueConstraints(Connection conn, TableHolder dbHolder) throws SQLException {
        if (dbHolder.getUniques() == null || dbHolder.getUniques().isEmpty()) {
            return;
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
            System.out.println("✓ Таблица " + dbHolder.getSchema() + "." + dbHolder.getName() + ": " + rowCount + " записей");
        }
    }


//    @Test
//    void compareIdenticalPlans() throws Exception{
//        String planA = readResourceFile("identPlans/planA.json");
//        String planB = readResourceFile("identPlans/planB.json");
//
//        PlanTree actualTree = PlanTree.fromJson(planA);
//        PlanTree expectedTree = PlanTree.fromJson(planB);
//
//        // Вычисление расстояния редактирования деревьев
//        float similarity = TreeEditDistance.computeSimilarity(actualTree.root, expectedTree.root);
//
//        System.out.println("Совпадение плано на " + similarity + "%");
//    }
}
