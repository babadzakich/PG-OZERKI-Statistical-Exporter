package ru.nsu.datagen.importer;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import ru.nsu.datagen.dataGenerator.model.TableMetadataMaker;

import java.io.*;
import java.sql.*;
import java.util.Map;

@Slf4j
public class Importer {
    static public Map<String, TableMetadata> startImport(String schemasScriptPath, String statisticDataPath, Connection conn) {
        try {
            Statement statement = conn.createStatement();
            importSchemas(schemasScriptPath, statement);
            return importStatistic(statisticDataPath);
        } catch (ImporterException e) {
            log.error("Importer exception occurred.", e);
            throw e;
        } catch (SQLException e) {
            log.error("SQL exception occurred during import.", e);
            throw new RuntimeException(e);
        }
    }

    static private Map<String, TableMetadata> importStatistic(String path) {
        if (!new File(path).exists()) {
            throw new ImporterException("There is no import statistic file " + path );
        }
        try {
            FileReader filereader = new FileReader(path);
            return TableMetadataMaker.processTableMetadata(filereader);
        } catch (RuntimeException | IOException e) {
            log.error("Cannot import statistic from CSV file.", e);
            throw new ImporterException("Cannot import statistic from CSV file.");
        }
    }

    static private void importSchemas(String path, Statement statement) throws ImporterException {
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
                        log.error("Error during SQL statement execution. {} {}", e.getMessage(), query.toString().trim());


                        throw new ImporterException("Cannot execute SQL script for schema import.");

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
