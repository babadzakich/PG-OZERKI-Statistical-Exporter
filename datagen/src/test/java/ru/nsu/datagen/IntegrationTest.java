package ru.nsu.datagen;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import org.junit.jupiter.api.Test;
import org.yaml.snakeyaml.Yaml;

import lombok.Getter;
import ru.nsu.datagen.dataGenerator.DatabaseDataGenerator;
import ru.nsu.datagen.importer.Importer;

import java.util.Collections;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;

/**
 * Интеграционный тест для проверки всего pipeline генерации
 * данных.
 * Использует локальную PostgreSQL БД.
 * Перед запуском теста убедитесь что БД доступна
 */
public class IntegrationTest {

    /**
     * Основной регрессионный тест, проверяющий работу всего pipeline:
     * 1. Создание схемы БД
     * 2. Импорт статистики
     * 3. Генерация данных
     * 4. Проверка целостности данных и ограничений
     * (5.) Запуск запроса
     * (6.) Проверка соответствия планов
     */
    @Test
    void testFullPipelineIntegration() throws Exception {
        Config config = loadConfig("config.yaml");
        ClassLoader classLoader = getClass().getClassLoader();
        String schemaPath = Paths.get(classLoader.getResource(config.getSCHEMA_PATH()).toURI()).toString();
        String statsPath = Paths.get(classLoader.getResource(config.getSTATS_PATH()).toURI()).toString();

        try (Connection conn = DriverManager.getConnection(config.getDB_URL(), config.getDB_USER(), config.getDB_PASSWORD())) {
            // Очищаем БД перед тестом
            try (Statement stmt = conn.createStatement()) {
                for (var dbHolder : config.getTables()) {
                    stmt.execute("DROP TABLE IF EXISTS " + dbHolder.getSchema() + "." + dbHolder.getName());
                }
            }

            // Импорт схемы и статистики
            List<String[]> rawImportedData = Importer.startImport(schemaPath, statsPath, conn);
            assertNotNull(rawImportedData, "Импортированные данные не должны быть null ");
            assertFalse(rawImportedData.isEmpty(), "Импортированные данные не должны быть пустыми ");
            System.out.println("✓ Импорт схемы и статистики выполнен успешно ");

            // Генерация данных
            DatabaseDataGenerator.generateData(rawImportedData, conn);
            System.out.println("✓ Генерация данных завершена ");

            // Проверка целостности данных и ограничений
            for (var dbHolder : config.getTables()) {
                checkTableIntegrity(conn, dbHolder);
            }

            // (5.) Запуск запроса и получение плана выполнения
            System.out.println("\n======================================== ");
            System.out.println("Проверка планов выполнения... ");
            String query = readQueryFromFile(classLoader, config.getQUERY_PATH());
            PlanNode actualPlan = executeExplainAnalyze(conn, query);
            assertNotNull(actualPlan, "План выполнения не должен быть null");
            System.out.println("✓ План выполнения получен ");

            // (6.) Загрузка исходного плана и сравнение
            PlanNode expectedPlan = loadPlanFromFile(classLoader, config.getSOURCE_PLAN_PATH());
            assertNotNull(expectedPlan, "Исходный план не должен быть null");

            int distance = TreeEditDistance.compute(expectedPlan, actualPlan);
            System.out.println("✓ Исходный план загружен ");

            System.out.println("\n======================================== ");
            System.out.println("Результат TreeEditDistance: " + distance);
            if (distance == 0) {
                System.out.println("✓ Планы ПОЛСНОСТЬЮ идентичны ");
            } else {
                System.out.println("⚠ Планы отличаются (расстояние = " + distance + ") ");
            }

            System.out.println("\n======================================== ");
            System.out.println("✓✓✓ ВСЕ ПРОВЕРКИ ПРОЙДЕНЫ УСПЕШНО ✓✓✓ ");
            System.out.println("======================================== ");
        }
    }

    /**
     *     Читает SQL запрос из файла
     */
    private String readQueryFromFile(ClassLoader classLoader, String queryPath) throws IOException {
        if (queryPath == null || queryPath.isEmpty()) {
            throw new IllegalArgumentException("Query path cannot be null or empty");
        }
        InputStream queryStream = classLoader.getResourceAsStream(queryPath);
        if (queryStream == null) {
            throw new RuntimeException("Query file not found: " + queryPath);
        }
        return new String(queryStream.readAllBytes(), java.nio.charset.StandardCharsets.UTF_8).trim();
    }

    /**
     * Выполняет EXPLAIN ANALYZE и возвращает корневой узел плана
     */
    private PlanNode executeExplainAnalyze(Connection conn, String query) throws SQLException {
        String explainQuery = "EXPLAIN (ANALYZE, FORMAT JSON) " + query;
        try (Statement stmt = conn.createStatement();
             ResultSet rs = stmt.executeQuery(explainQuery)) {
            if (rs.next()) {
                String jsonResult = rs.getString(1);
                ObjectMapper mapper = new ObjectMapper();
                ExplainRoot root = mapper.readValue(jsonResult, ExplainRoot.class);
                return root.plan;
            }
        }
        return null;
    }
    /**
    * Загружает план из JSON файла
     */
    private PlanNode loadPlanFromFile(ClassLoader classLoader, String planPath) throws IOException {
        if (planPath == null || planPath.isEmpty()) {
            throw new IllegalArgumentException("Plan path cannot be null or empty");
        }
        InputStream planStream = classLoader.getResourceAsStream(planPath);
        if (planStream == null) {
            throw new RuntimeException("Plan file not found: " + planPath);
        }
        ObjectMapper mapper = new ObjectMapper();
        ExplainRoot root = mapper.readValue(planStream, ExplainRoot.class);
        return root.plan;
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

    class TreeEditDistance {
        float deleteCost = 1f;
        float relable = 1f;
        float insert = 1f;

        public static int compute(PlanNode tree1, PlanNode tree2) {
            if (tree1 == null && tree2 == null) return 0;
            if (tree1 == null) return costInsertTree(tree2);
            if (tree2 == null) return costDeleteTree(tree1);
            return forestDistance(Collections.singletonList(tree1), Collections.singletonList(tree2));
        }


        private static int costDeleteTree(PlanNode node) {
            int cost = 1;
            for (PlanNode child : safeGetPlans(node)) {
                cost += costDeleteTree(child);
            }
            return cost;
        }

        private static int costInsertTree(PlanNode node) {
            int cost = 1;
            for (PlanNode child : safeGetPlans(node)) {
                cost += costInsertTree(child);
            }
            return cost;
        }

        private static int costReplace(PlanNode a, PlanNode b) {
            return a.nodeType.equals(b.nodeType) ? 0 : 1;
        }

        private static int forestDistance(List<PlanNode> forest1, List<PlanNode> forest2) {
            int n = forest1.size();
            int m = forest2.size();
            int[][] dp = new int[n + 1][m + 1];

            // Заполняем таблицу снизу вверх
            for (int i = n; i >= 0; i--) {
                for (int j = m; j >= 0; j--) {
                    if (i == n && j == m) {
                        dp[i][j] = 0;
                    } else if (i == n) {
                        // Вставка оставшихся из forest2
                        int cost = 0;
                        for (int k = j; k < m; k++) {
                            cost += costInsertTree(forest2.get(k));
                        }
                        dp[i][j] = cost;
                    } else if (j == m) {
                        // Удаление оставшихся из forest1
                        int cost = 0;
                        for (int k = i; k < n; k++) {
                            cost += costDeleteTree(forest1.get(k));
                        }
                        dp[i][j] = cost;
                    } else {
                        int deleteOption = dp[i + 1][j] + costDeleteTree(forest1.get(i));
                        int insertOption = dp[i][j + 1] + costInsertTree(forest2.get(j));
                        int childrenDist = forestDistance(
                                safeGetPlans(forest1.get(i)),
                                safeGetPlans(forest2.get(j))
                        );
                        int replaceOption = childrenDist
                                + costReplace(forest1.get(i), forest2.get(j))
                                + dp[i + 1][j + 1];

                        dp[i][j] = Math.min(deleteOption, Math.min(insertOption, replaceOption));
                    }
                }
            }
            return dp[0][0];
        }

        private static List<PlanNode> safeGetPlans(PlanNode node) {
            if (node == null || node.plans == null) {
                return Collections.emptyList();
            }
            return node.plans;
        }
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    class ExplainRoot {
        @JsonProperty("Plan")
        public PlanNode plan;
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    class PlanNode {
        @JsonProperty("Node Type")
        public String nodeType;

        @JsonProperty("Plans")
        public List<PlanNode> plans;

        public List<PlanNode> getPlans() {
            if (plans == null) {
                plans = new ArrayList<>();
            }
            return plans;
        }
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    class ExplainRoot {
        @JsonProperty("Plan")
        public PlanNode plan;
    }
    /**
     * Конфигурация базы данных и таблиц для теста
     * 
     * Хранит информацию о подключении к БД и ожидаемых таблицах
     * которые должны быть сгенерированы и проверены в тесте
     */
    @Getter
    public static class Config {
        private final String DB_URL;
        private final String DB_USER;
        private final String DB_PASSWORD;
        private final String SCHEMA_PATH;
        private final String STATS_PATH;
        private final List<TableHolder> tables;
        private final String QUERY_PATH;
        private final String SOURCE_PLAN_PATH;

        public Config(String db_url, String db_user, String db_password, String schema_path, String stats_path, String query_path, String source_plan_path, List<TableHolder> tables) {
            this.DB_URL = db_url;
            this.DB_USER = db_user;
            this.DB_PASSWORD = db_password;
            this.SCHEMA_PATH = schema_path;
            this.STATS_PATH = stats_path;
            this.tables = tables;
            this.QUERY_PATH = query_path;
            this.SOURCE_PLAN_PATH = source_plan_path;
        }
    }
    /**
     * Хранит информацию о таблице для теста
     * Имя таблицы, схема, ожидаемый размер и ограничения
     */
    @Getter
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

    /**
     * Загружает конфигурацию из YAML файла в ресурсах теста
     * Конфигурация включает параметры подключения к БД
     * и описание таблиц для генерации и проверки.
     *
     * @param configPath путь к YAML файлу конфигурации
     */
    @SuppressWarnings("unchecked")
    Config loadConfig(String configPath) throws IOException{
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

            return new Config(
                (String) configMap.get("db_url"),
                (String) configMap.get("user"),
                (String) configMap.get("password"),
                (String) configMap.get("schema"),
                (String) configMap.get("statistic"),
                (String) configMap.get("query_path"),
                (String) configMap.get("source_plan_path"),
                databases
            );
        } 
    }
}
