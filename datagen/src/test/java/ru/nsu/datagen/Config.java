package ru.nsu.datagen;

import lombok.Getter;
import org.yaml.snakeyaml.Yaml;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

@Getter
public class Config {
    private final String DB_NAME;
    private final String SCHEMA_PATH;
    private final String STATS_PATH;
    private final String queryPath;
    private final String sourcePlanPath;

    private final List<TableHolder> tables;

    /**
     * Загружает конфигурацию из YAML файла в ресурсах теста
     * Конфигурация включает параметры подключения к БД
     * и описание таблиц для генерации и проверки.
     *
     * @param configPath путь к YAML файлу конфигурации
     */
    Config(String configPath) throws IOException {
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

            List<Map<String, Object>> databasesRaw = castList(configMap.get("tables"), Map.class,
                    "tables must be a list of maps");
            List<TableHolder> databases = new ArrayList<>();

            if (databasesRaw != null) {
                for (Map<String, Object> dbRaw : databasesRaw) {
                    String schema = (String) dbRaw.getOrDefault("schema", "public");
                    String name = (String) dbRaw.get("name");
                    long expectedCount = ((Number) dbRaw.get("expectedCount")).longValue();
                    List<List<String>> uniques = castList(dbRaw.get("uniques"), List.class,
                            "uniques must be a list of lists");
                    List<String> pk = castList(dbRaw.get("pk"), String.class,
                            "pk must be a list of strings");
                    databases.add(new TableHolder(name, schema, expectedCount, uniques, pk));
                }
            }

            this.DB_NAME = (String) configMap.get("dbName");
            this.SCHEMA_PATH = (String) configMap.get("schema");
            this.STATS_PATH = (String) configMap.get("statistic");
            this.tables = databases;
            this.queryPath = (String) configMap.get("query_path");
            this.sourcePlanPath = (String) configMap.get("source_plan_path");
        }
    }

    @SuppressWarnings("unchecked")
    private static <T> List<T> castList(Object obj, Class<?> elementClazz, String errorMessage) {
        if (obj == null) return null;
        if (obj instanceof List<?> list) {
            list.forEach(item -> {
                if (item != null && !elementClazz.isInstance(item)) {
                    throw new ClassCastException(errorMessage +
                        ": expected " + elementClazz.getName() + ", got " + item.getClass().getName()
                    );
                }
            });
            return (List<T>) list;
        }
        throw new ClassCastException(errorMessage + ": expected List, got " + obj.getClass().getName());
    }
}
