package ru.nsu.datagen;

import java.io.File;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import org.junit.jupiter.api.Test;

import ru.nsu.datagen.dataGenerator.DatabaseDataGenerator;
import ru.nsu.datagen.importer.Importer;

/**
 * Регрессионный интеграционный тест для проверки всего pipeline генерации
 * данных.
 * Использует локальную PostgreSQL БД.
 */
public class RegressionTest {

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
        String schemaPath = new File(classLoader.getResource("schema.sql").getFile()).getAbsolutePath();
        String statsPath = new File(classLoader.getResource("stats.csv").getFile()).getAbsolutePath();

        String jdbcUrl = System.getenv().getOrDefault("DB_URL", "jdbc:postgresql://localhost:5432/testdb");
        String username = System.getenv().getOrDefault("DB_USER", "kubicl");
        String password = System.getenv().getOrDefault("DB_PASSWORD", "postgres");

        try (Connection conn = DriverManager.getConnection(jdbcUrl, username, password)) {
            try (Statement stmt = conn.createStatement()) {
                stmt.execute("DROP SCHEMA IF EXISTS public CASCADE");
                stmt.execute("CREATE SCHEMA public");
            }
            List<String[]> rawImportedData = Importer.startImport(schemaPath, statsPath, conn);

            assertNotNull(rawImportedData, "Импортированные данные не должны быть null");
            assertFalse(rawImportedData.isEmpty(), "Импортированные данные не должны быть пустыми");
            System.out.println("✓ Импорт схемы и статистики выполнен успешно");

            DatabaseDataGenerator.generateData(rawImportedData, conn);
            System.out.println("✓ Генерация данных завершена");

            try (Statement stmt = conn.createStatement()) {
                ResultSet rs = stmt.executeQuery("SELECT COUNT(*) FROM public.product_review");
                rs.next();
                int productReviewCount = rs.getInt(1);
                assertEquals(500000, productReviewCount,
                        "В таблице product_review должно быть 500000 записей");
                System.out.println("✓ Таблица product_review: " + productReviewCount + " записей");
            }

            try (Statement stmt = conn.createStatement()) {
                ResultSet rs = stmt.executeQuery(
                        "SELECT COUNT(DISTINCT id) as distinct_count, COUNT(*) as total_count " +
                                "FROM public.product_review");
                rs.next();
                int distinctCount = rs.getInt("distinct_count");
                int totalCount = rs.getInt("total_count");
                assertEquals(totalCount, distinctCount,
                        "Все id в product_review должны быть уникальными (PK)");
                System.out.println("✓ Первичные ключи product_review уникальны");
            }

            try (Statement stmt = conn.createStatement()) {
                ResultSet rs = stmt.executeQuery(
                        "SELECT COUNT(*) as duplicate_count FROM (" +
                                "  SELECT user_id, product_id, COUNT(*) as cnt " +
                                "  FROM public.product_review " +
                                "  GROUP BY user_id, product_id " +
                                "  HAVING COUNT(*) > 1" +
                                ") duplicates");
                rs.next();
                int duplicateCount = rs.getInt("duplicate_count");
                assertEquals(0, duplicateCount,
                        "Пары (user_id, product_id) должны быть уникальными");
                System.out.println("✓ UNIQUE constraint (user_id, product_id) соблюден");
            }


            try (Statement stmt = conn.createStatement()) {
                ResultSet rs = stmt.executeQuery(
                        "SELECT COUNT(*) FROM public.product_review " +
                                "WHERE id IS NULL OR user_id IS NULL OR product_id IS NULL");
                rs.next();
                int nullCount = rs.getInt(1);
                assertEquals(0, nullCount,
                        "NOT NULL колонки не должны содержать NULL значения");
                System.out.println("✓ NOT NULL constraints соблюдены для product_review");
            }

            try (Statement stmt = conn.createStatement()) {
                ResultSet rs = stmt.executeQuery(
                        "SELECT COUNT(*) FROM public.product_review WHERE id <= 0");
                rs.next();
                int invalidIds = rs.getInt(1);
                assertEquals(0, invalidIds, "Все id должны быть положительными");
            }

            int testProductReviewId;
            try (Statement stmt = conn.createStatement()) {
                ResultSet rs = stmt.executeQuery(
                        "SELECT id FROM public.product_review LIMIT 1");
                assertTrue(rs.next(), "Должна быть хотя бы одна запись в product_review");
                testProductReviewId = rs.getInt("id");
            }

            try (Statement stmt = conn.createStatement()) {
                stmt.executeUpdate(
                        "DELETE FROM public.product_review WHERE id = " + testProductReviewId);
            }

            System.out.println("\n========================================");
            System.out.println("✓✓✓ ВСЕ ПРОВЕРКИ ПРОЙДЕНЫ УСПЕШНО ✓✓✓");
            System.out.println("========================================");
        }
    }
}
