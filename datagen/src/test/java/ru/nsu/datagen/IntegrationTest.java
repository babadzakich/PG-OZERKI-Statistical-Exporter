package ru.nsu.datagen;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.io.BufferedWriter;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.stream.Collectors;

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
 * Использует Testcontainers для поднятия изолированной PostgreSQL 17, импортирует схему и статистику,
 * генерирует данные и проверяет целостность и планы запросов.
 */
@Slf4j
@Testcontainers
public class IntegrationTest {

    @Container
    private static final PostgreSQLContainer<?> postgres =
            new PostgreSQLContainer<>("postgres:17");

    private static HikariDataSource dataSource;
    private Config config;
    private String configName;

    @BeforeAll
    static void beforeAll() throws SQLException {
        try {
            System.setProperty("api.version", "1.44");
            HikariConfig hikariConfig = new HikariConfig();
            hikariConfig.setJdbcUrl(postgres.getJdbcUrl());
            hikariConfig.setUsername(postgres.getUsername());
            hikariConfig.setPassword(postgres.getPassword());
            hikariConfig.setMaximumPoolSize(32);
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
        configName = configPath.replaceFirst("^.*/", "").replace(".yaml", "");
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
    @ConfigFile("timetable/timetable.yaml")
    void testFullPipelineIntegration() throws Exception {
        ClassLoader classLoader = getClass().getClassLoader();
        String schemaPath = Paths.get(classLoader.getResource(config.getSCHEMA_PATH()).toURI()).toString();
        String statsPath = Paths.get(classLoader.getResource(config.getSTATS_PATH()).toURI()).toString();
        String indexPath = null;
        int globStoreThreads = config.getGlobStoreThreads();
        int tableStoreThreads = config.getTableStoreThreads();
        int batchSize = config.getBatchSize();
        if (config.getIndexPath() != null) {
            indexPath = Paths.get(classLoader.getResource(config.getIndexPath()).toURI()).toString();
        }
        String constraintPath = Paths.get(classLoader.getResource(config.getCONSTRAINT_PATH()).toURI()).toString();
        try (Connection conn = dataSource.getConnection()) {
            // Импорт схемы и статистики
            Map<String, TableMetadata> importedData = Importer.startImport(schemaPath, statsPath, constraintPath, conn);

            Optional.ofNullable(indexPath).ifPresent(path -> {
                try {
                    Importer.importSchemas(path, conn.createStatement());
                } catch (SQLException e) {
                    log.error("Failed to import indexes from SQL file.", e);
                    throw new RuntimeException(e);
                }
            });
            assertNotNull(importedData, "Импортированные данные не должны быть null");
            assertFalse(importedData.isEmpty(), "Импортированные данные не должны быть пустыми");
            System.out.println("✓ Импорт схемы и статистики выполнен успешно");
            ExecutorService executorService = Executors.newFixedThreadPool(Runtime.getRuntime().availableProcessors());
            // Генерация данных
            DatabaseDataGenerator.generateData(importedData, dataSource, executorService, batchSize, globStoreThreads, tableStoreThreads);
            System.out.println("✓ Генерация данных завершена");


            // Проверка целостности данных и ограничений
            for (var dbHolder : config.getTables()) {
                checkTableIntegrity(conn, dbHolder);
            }

            // Экспорт статистики из pg_stats
            exportPgStats(conn);

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

    private void exportPgStats(Connection conn) throws SQLException, IOException {
        try (Statement analyzeStmt = conn.createStatement()) {
            analyzeStmt.execute("ANALYZE");
        }

        if (config.getTables() == null || config.getTables().isEmpty()) {
            return;
        }

        String tableFilter = config.getTables().stream()
                .map(t -> "(cb.table_schema = '" + t.getSchema() + "' AND cb.table_name = '" + t.getName() + "')")
                .collect(Collectors.joining(" OR "));

        Path outputDir = Paths.get(System.getProperty("user.dir"), "build", "stats-export");
        Files.createDirectories(outputDir);
        Path outputFile = outputDir.resolve(configName + "_stats.csv");

        try (Statement stmt = conn.createStatement();
             ResultSet rs = stmt.executeQuery(buildStatsExportSql(tableFilter));
             BufferedWriter bw = Files.newBufferedWriter(outputFile, StandardCharsets.UTF_8);
             PrintWriter writer = new PrintWriter(bw)) {

            writer.println("table_schema,table_name,column_name,data_type,row_count,null_percent,modifiers," +
                    "composite_unique_peers,composite_fk_peers,incoming_references,max_length," +
                    "outcoming_references,relation_types,mcv,mcv_frequencies,avg_column_width_bytes,ndistinct,hbounds");

            while (rs.next()) {
                writer.println(String.join(",",
                        csvField(rs.getString("table_schema")),
                        csvField(rs.getString("table_name")),
                        csvField(rs.getString("column_name")),
                        csvField(rs.getString("data_type")),
                        csvField(rs.getString("row_count")),
                        csvField(rs.getString("null_percent")),
                        csvField(rs.getString("modifiers")),
                        csvField(rs.getString("composite_unique_peers")),
                        csvField(rs.getString("composite_fk_peers")),
                        csvField(rs.getString("incoming_references")),
                        csvField(rs.getString("max_length")),
                        csvField(rs.getString("outcoming_references")),
                        csvField(rs.getString("relation_types")),
                        csvField(rs.getString("mcv")),
                        csvField(rs.getString("mcv_frequencies")),
                        csvField(rs.getString("avg_column_width_bytes")),
                        csvField(rs.getString("ndistinct")),
                        csvField(rs.getString("hbounds"))
                ));
            }
        }

        log.info("Exported pg_stats to {}", outputFile.toAbsolutePath());
        System.out.println("✓ Статистика pg_stats экспортирована в " + outputFile.toAbsolutePath());
    }

    private String buildStatsExportSql(String tableFilter) {
        return """
                WITH
                table_counts AS (
                    SELECT n.nspname AS schemaname, c.relname AS table_name, c.reltuples::bigint AS row_count
                    FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
                    WHERE c.relkind = 'r' AND n.nspname NOT IN ('pg_catalog', 'information_schema')
                ),
                pk_cols AS (
                    SELECT con.conrelid, unnest(con.conkey) AS attnum
                    FROM pg_constraint con WHERE con.contype = 'p'
                ),
                unique_cons AS (
                    SELECT con.conrelid, con.conkey
                    FROM pg_constraint con WHERE con.contype = 'u'
                ),
                fk_cons AS (
                    SELECT con.conrelid, con.confrelid, con.conkey, con.confkey,
                           src_tbl.relname AS src_table, src_ns.nspname AS src_schema,
                           ref_tbl.relname AS ref_table, ref_ns.nspname AS ref_schema
                    FROM pg_constraint con
                    JOIN pg_class src_tbl ON src_tbl.oid = con.conrelid
                    JOIN pg_namespace src_ns ON src_ns.oid = src_tbl.relnamespace
                    JOIN pg_class ref_tbl ON ref_tbl.oid = con.confrelid
                    JOIN pg_namespace ref_ns ON ref_ns.oid = ref_tbl.relnamespace
                    WHERE con.contype = 'f'
                ),
                col_base AS (
                    SELECT n.nspname AS table_schema, c.relname AS table_name, a.attname AS column_name,
                           pg_catalog.format_type(a.atttypid, a.atttypmod) AS data_type,
                           a.attnum,
                           CASE WHEN a.atttypid IN (1042, 1043, 25) THEN
                               CASE WHEN a.atttypmod = -1 THEN -1 ELSE a.atttypmod - 4 END
                           ELSE -1 END AS max_length,
                           c.oid AS reloid
                    FROM pg_attribute a
                    JOIN pg_class c ON a.attrelid = c.oid
                    JOIN pg_namespace n ON c.relnamespace = n.oid
                    WHERE a.attnum > 0 AND NOT a.attisdropped AND c.relkind = 'r'
                      AND n.nspname NOT IN ('pg_catalog', 'information_schema')
                )
                SELECT
                    cb.table_schema,
                    cb.table_name,
                    cb.column_name,
                    cb.data_type,
                    tc.row_count,
                    ROUND((COALESCE(s.null_frac, 0) * 100)::numeric, 2) AS null_percent,
                    NULLIF(TRIM(
                        CASE WHEN EXISTS(SELECT 1 FROM pk_cols pk WHERE pk.conrelid = cb.reloid AND pk.attnum = cb.attnum) THEN 'PK ' ELSE '' END ||
                        CASE WHEN EXISTS(SELECT 1 FROM fk_cons fk WHERE fk.conrelid = cb.reloid AND cb.attnum = ANY(fk.conkey)) THEN 'FK ' ELSE '' END ||
                        CASE WHEN EXISTS(SELECT 1 FROM unique_cons uc WHERE uc.conrelid = cb.reloid AND cb.attnum = ANY(uc.conkey)) THEN 'UNIQUE ' ELSE '' END ||
                        CASE WHEN EXISTS(SELECT 1 FROM pg_constraint cc WHERE cc.conrelid = cb.reloid AND cc.contype = 'c' AND cb.attnum = ANY(cc.conkey)) THEN 'CHECK' ELSE '' END
                    ), '') AS modifiers,
                    (SELECT string_agg(peer_ns.nspname || '.' || peer_cls.relname || '.' || peer_attr.attname, ',')
                     FROM unique_cons uc
                     CROSS JOIN LATERAL unnest(uc.conkey) AS peer_attnum
                     JOIN pg_attribute peer_attr ON peer_attr.attrelid = uc.conrelid
                                                 AND peer_attr.attnum = peer_attnum
                                                 AND peer_attr.attname != cb.column_name
                     JOIN pg_class peer_cls ON peer_cls.oid = uc.conrelid
                     JOIN pg_namespace peer_ns ON peer_ns.oid = peer_cls.relnamespace
                     WHERE uc.conrelid = cb.reloid AND cb.attnum = ANY(uc.conkey) AND array_length(uc.conkey, 1) > 1
                    ) AS composite_unique_peers,
                    (SELECT string_agg(fk_ns.nspname || '.' || fk_cls.relname || '.' || fk_attr.attname, ',')
                     FROM fk_cons fk
                     CROSS JOIN LATERAL unnest(fk.conkey) AS fk_attnum
                     JOIN pg_attribute fk_attr ON fk_attr.attrelid = fk.conrelid
                                               AND fk_attr.attnum = fk_attnum
                                               AND fk_attr.attname != cb.column_name
                     JOIN pg_class fk_cls ON fk_cls.oid = fk.conrelid
                     JOIN pg_namespace fk_ns ON fk_ns.oid = fk_cls.relnamespace
                     WHERE fk.conrelid = cb.reloid AND cb.attnum = ANY(fk.conkey) AND array_length(fk.conkey, 1) > 1
                    ) AS composite_fk_peers,
                    (SELECT string_agg(fk.src_schema || '.' || fk.src_table || '.' || ia.attname, ' ')
                     FROM fk_cons fk
                     JOIN pg_attribute ia ON ia.attrelid = fk.conrelid
                       AND ia.attnum = fk.conkey[array_position(fk.confkey, cb.attnum::smallint)]
                     WHERE fk.confrelid = cb.reloid AND cb.attnum = ANY(fk.confkey)
                    ) AS incoming_references,
                    cb.max_length,
                    (SELECT fk.ref_schema || '.' || fk.ref_table || '.' || ra.attname
                     FROM fk_cons fk
                     JOIN pg_attribute ra ON ra.attrelid = fk.confrelid
                       AND ra.attnum = fk.confkey[array_position(fk.conkey, cb.attnum::smallint)]
                     WHERE fk.conrelid = cb.reloid AND cb.attnum = ANY(fk.conkey)
                     LIMIT 1
                    ) AS outcoming_references,
                    (SELECT CASE
                         WHEN (EXISTS(SELECT 1 FROM pk_cols pk WHERE pk.conrelid = cb.reloid AND pk.attnum = cb.attnum)
                            OR EXISTS(SELECT 1 FROM unique_cons uc WHERE uc.conrelid = cb.reloid AND cb.attnum = ANY(uc.conkey)))
                         THEN 'ONE_TO_ONE' ELSE 'ONE_TO_MANY'
                     END
                     FROM fk_cons fk WHERE fk.conrelid = cb.reloid AND cb.attnum = ANY(fk.conkey)
                     LIMIT 1
                    ) AS relation_types,
                    s.most_common_vals::text AS mcv,
                    s.most_common_freqs::text AS mcv_frequencies,
                    COALESCE(s.avg_width, 0) AS avg_column_width_bytes,
                    s.n_distinct AS ndistinct,
                    s.histogram_bounds::text AS hbounds
                FROM col_base cb
                JOIN table_counts tc ON tc.schemaname = cb.table_schema AND tc.table_name = cb.table_name
                LEFT JOIN pg_stats s ON s.schemaname = cb.table_schema
                                     AND s.tablename = cb.table_name
                                     AND s.attname = cb.column_name
                WHERE \s""" + tableFilter + """
                ORDER BY cb.table_schema, cb.table_name, cb.attnum
                """;
    }

    private String csvField(String value) {
        if (value == null) return "NULL";
        if (value.contains(",") || value.contains("\"") || value.contains("\n") || value.contains("\r")) {
            return "\"" + value.replace("\"", "\"\"") + "\"";
        }
        return value;
    }
}
