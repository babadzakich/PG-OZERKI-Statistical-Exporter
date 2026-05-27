package ru.nsu.datagen.importer;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadataMaker;

/**
 * Отвечает за импорт входных файлов в начале пайплайна.
 *
 * <p>Выполняет два шага:
 * <ol>
 *   <li>Применяет schema.sql к БД через {@link #importSchemas} (создаёт таблицы, типы, расширения).</li>
 *   <li>Парсит stats CSV и constraints CSV в карту {@link TableMetadata} через
 *       {@link ru.nsu.datagen.dataGenerator.model.TableMetadataMaker}.</li>
 * </ol>
 *
 * <p>SQL-скрипт выполняется построчно; ошибки {@code 42P01} (relation does not exist)
 * при DROP-операциях логируются как предупреждение и не прерывают выполнение.
 */
@Slf4j
public class Importer {
    /**
     * Применяет схему к БД и парсит CSV-статистику.
     *
     * @param schemasScriptPath  путь к SQL-скрипту создания схемы
     * @param statisticDataPath  путь к CSV-файлу со статистикой колонок
     * @param constraintsDataPath путь к CSV-файлу с ограничениями (PK/UNIQUE/FK)
     * @param conn               соединение с целевой БД
     * @return карта {@code schema.tableName -> TableMetadata} для всех таблиц
     */
    static public Map<String, TableMetadata> startImport(String schemasScriptPath, String statisticDataPath, String constraintsDataPath, Connection conn) {
        try {
            Statement statement = conn.createStatement();
            importSchemas(schemasScriptPath, statement);
            return importStatistic(statisticDataPath, constraintsDataPath);
        } catch (ImporterException e) {
            log.error("Importer exception occurred.", e);
            throw e;
        } catch (SQLException e) {
            log.error("SQL exception occurred during import.", e);
            throw new RuntimeException(e);
        }
    }

    static private Map<String, TableMetadata> importStatistic(String statPath, String constrPath) {
        if (!new File(statPath).exists()) {
            throw new ImporterException("There is no import statistic file " + statPath );
        }
        if (!new File(constrPath).exists()) {
            throw new ImporterException("There is no import constraints file " + constrPath );
        }
        try {
            FileReader filereader = new FileReader(statPath);
            FileReader constrReader = new FileReader(constrPath);
            return TableMetadataMaker.processTableMetadata(filereader, constrReader);
        } catch (RuntimeException | IOException e) {
            log.error("Cannot import statistic from CSV file.", e);
            throw new ImporterException("Cannot import statistic from CSV file.");
        }
    }

    /**
     * Выполняет SQL-скрипт из файла {@code path} через переданный {@link Statement}.
     * Используется как для схемы, так и для индексов (после генерации данных).
     *
     * @param path      путь к SQL-файлу
     * @param statement JDBC statement для выполнения
     * @throws ImporterException если файл не найден или SQL завершился с ошибкой
     */
    static public void importSchemas(String path, Statement statement) throws ImporterException {
        if (!new File(path).exists()) {
            throw new ImporterException("There is no import schemas script " + path);
        }
        try {
            executeSqlScript(path, statement);
        } catch (IOException e) {
            log.error("Cannot import schemas from SQL file.", e);
            throw new ImporterException("Cannot import schemas from SQL file.");
        } catch (SQLException e) {
            log.error("Cannot execute SQL script for schema import. {}", e.getSQLState());
            throw new ImporterException("Cannot execute SQL script for schema import.");

        }
    }

    static private void executeSqlScript(String path, Statement statement) throws SQLException, IOException {
        try (BufferedReader br = new BufferedReader(new FileReader(path))) {
            // String Builder to build the query line by line.
            StringBuilder query = new StringBuilder();
            String line;
            while ((line = br.readLine()) != null) {
                if (line.trim().startsWith("--")) { continue; }

                // Append the line into the query string and add a space after that
                query.append(line).append(" ");

                if (line.trim().endsWith(";")) {
                    // Execute the Query

                    try {
                        statement.execute(query.toString().trim());
                    } catch (SQLException e) {

                        if (e.getSQLState() != null && e.getSQLState().equals("42P01")) {
                            log.warn("No such relation: {}", e.getMessage());
                        }

                        else {
                            log.error("Error during SQL statement execution. {} {} {}", e.getMessage(),
                                    query.toString().trim(), e.getSQLState());


                            throw new ImporterException("Cannot execute SQL script for schema import.");
                        }
                    }
                    // Empty the Query string to add new query from the file
                    query = new StringBuilder();
                }
            }

            log.debug("Script file {} executed", path);

            // Getting the ResultSet after executing the Script File
            ResultSet resultSet = statement.getResultSet();
            log.debug("Result set: {}", resultSet);
        }
    }
}
