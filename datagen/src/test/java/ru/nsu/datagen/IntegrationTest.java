package ru.nsu.datagen;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.zaxxer.hikari.HikariDataSource;
import org.junit.jupiter.api.Test;
import org.yaml.snakeyaml.Yaml;
import ru.nsu.datagen.dataGenerator.DatabaseDataGenerator;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.importer.Importer;
import ru.nsu.datagen.plancheck.struct.ExplainRoot;
import ru.nsu.datagen.plancheck.struct.PlanNode;
import ru.nsu.datagen.plancheck.struct.PlanTree;
import ru.nsu.datagen.plancheck.ted.TreeEditDistance;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;

public class IntegrationTest {
    private final HikariDataSource dataSource;

    public IntegrationTest() {
        Config config;
        try {
            config = loadConfig("config.yaml");
        } catch (IOException e) {
            throw new RuntimeException("Failed to load config", e);
        }
        this.dataSource = new HikariDataSource();
        this.dataSource.setJdbcUrl(config.DB_URL());
        this.dataSource.setUsername(config.DB_USER());
        this.dataSource.setPassword(config.DB_PASSWORD());
    }

    @Test
    void testFullPipelineIntegration() throws Exception {
        Config config = loadConfig("config.yaml");
        ClassLoader classLoader = getClass().getClassLoader();
        String schemaPath = Paths.get(classLoader.getResource(config.SCHEMA_PATH()).toURI()).toString();
        String statsPath = Paths.get(classLoader.getResource(config.STATS_PATH()).toURI()).toString();

        try (Connection conn = dataSource.getConnection()) {
            // Очищаем БД перед тестом
            try (Statement stmt = conn.createStatement()) {
                for (var dbHolder : config.tables()) {
                    stmt.execute("DROP TABLE IF EXISTS " + dbHolder.getName() + " CASCADE");
                }
            }

            // Импорт схемы и статистики
            List<TableMetadata> importedData = Importer.startImport(schemaPath, statsPath, conn);
            assertNotNull(importedData, "Импортированные данные не должны быть null");
            assertFalse(importedData.isEmpty(), "Импортированные данные не должны быть пустыми");
            System.out.println("✓ Импорт схемы и статистики выполнен успешно");

            // Генерация данных
            DatabaseDataGenerator.generateData(importedData, dataSource);
            System.out.println("✓ Генерация данных завершена");

            // Проверка целостности данных и ограничений
            for (var dbHolder : config.tables()) {
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
        String queryPath = config.queryPath();
        String sourcePlanPath = config.sourcePlanPath();
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
        assertTrue(similarity > 0.5, "Similarity gt 50%");

    }

    /**
     * Выполняет EXPLAIN (ANALYZE, FORMAT JSON) для заданного SQL-запроса.
     */
    private String executeExplainAnalyze(Connection conn, String sql) throws SQLException {
        String explainSql = "EXPLAIN (VERBOSE, FORMAT JSON) " + sql;
        try (Statement stmt = conn.createStatement();
             ) {
            stmt.execute("ANALYZE");
            ResultSet rs = stmt.executeQuery(explainSql);
            rs.next();
            // PostgreSQL возвращает JSON в первой колонке первой строки
            System.out.println(rs.getString(1));
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

    private void checkTableSize(Connection conn, TableHolder dbHolder) throws SQLException {
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

    public record Config(String DB_URL, String DB_USER, String DB_PASSWORD, String SCHEMA_PATH, String STATS_PATH,
                         List<TableHolder> tables, String queryPath, String sourcePlanPath) {
    }

    @lombok.Getter
    public static class TableHolder {
        private final String name;
        private final String schema;
        private long size;
        private final List<List<String>> uniques;
        private final List<String> pks;

        public TableHolder(String name, String schema, long size, List<List<String>> uniques, List<String> pks) {
            this.name = name;
            this.schema = schema;
            this.size = size;
            this.uniques = uniques;
            this.pks = pks;
        }
    }

    @SuppressWarnings("unchecked")
    Config loadConfig(String configPath) throws IOException {
        if (configPath == null || configPath.isEmpty()) {
            throw new IllegalArgumentException("Config path cannot be null or empty");
        }
        if (!configPath.endsWith(".yaml")) {
            throw new IllegalArgumentException("Config file must be a YAML file");
        }

        ClassLoader classLoader = getClass().getClassLoader();
        try (InputStream configFileStream = classLoader.getResourceAsStream(configPath)) {
            if (configFileStream == null) {
                throw new RuntimeException("Config file not found in resources: " + configPath);
            }
            Yaml yaml = new Yaml();
            Map<String, Object> configMap = yaml.load(configFileStream);

            List<Map<String, Object>> databasesRaw = (List<Map<String, Object>>) configMap.get("tables");
            List<TableHolder> databases = new ArrayList<>();

            if (databasesRaw != null) {
                for (Map<String, Object> dbRaw : databasesRaw) {
                    String schema = (String) dbRaw.getOrDefault("schema", "public");
                    String name = (String) dbRaw.get("name");
                    long expectedCount = ((Number) dbRaw.get("expectedCount")).longValue();
                    List<List<String>> uniques = (List<List<String>>) dbRaw.get("uniques");
                    List<String> pk = (List<String>) dbRaw.get("pk");
                    databases.add(new TableHolder(name, schema, expectedCount, uniques, pk));
                }
            }

            String queryPath = (String) configMap.get("query_path");
            String sourcePlanPath = (String) configMap.get("source_plan_path");

            return new Config(
                    (String) configMap.get("db_url"),
                    (String) configMap.get("user"),
                    (String) configMap.get("password"),
                    (String) configMap.get("schema"),
                    (String) configMap.get("statistic"),
                    databases,
                    queryPath,
                    sourcePlanPath
            );
        }
    }
}