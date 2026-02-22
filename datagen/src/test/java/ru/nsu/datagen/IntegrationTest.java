package ru.nsu.datagen;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;

import com.zaxxer.hikari.HikariDataSource;
import org.junit.jupiter.api.Test;
import org.yaml.snakeyaml.Yaml;

import lombok.Getter;
import ru.nsu.datagen.dataGenerator.DatabaseDataGenerator;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.importer.Importer;

/**
 * Интеграционный тест для проверки всего pipeline генерации
 * данных.
 * Использует локальную PostgreSQL БД.
 * Перед запуском теста убедитесь что БД доступна
 */
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
        this.dataSource.setJdbcUrl(config.getDB_URL());
        this.dataSource.setUsername(config.getDB_USER());
        this.dataSource.setPassword(config.getDB_PASSWORD());
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
        Config config = loadConfig("config.yaml");
        ClassLoader classLoader = getClass().getClassLoader();
        String schemaPath = Paths.get(classLoader.getResource(config.getSCHEMA_PATH()).toURI()).toString();
        String statsPath = Paths.get(classLoader.getResource(config.getSTATS_PATH()).toURI()).toString();

        try (Connection conn = dataSource.getConnection()) {
            // Очищаем БД перед тестом
            try (Statement stmt = conn.createStatement()) {
                for (var dbHolder : config.getTables()) {
                    stmt.execute("DROP TABLE IF EXISTS " + dbHolder.getName());
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

        public Config(String db_url, String db_user, String db_password, String schema_path, String stats_path, List<TableHolder> tables) {
            this.DB_URL = db_url;
            this.DB_USER = db_user;
            this.DB_PASSWORD = db_password;
            this.SCHEMA_PATH = schema_path;
            this.STATS_PATH = stats_path;
            this.tables = tables;
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
                databases
            );
        } 
    }
}
