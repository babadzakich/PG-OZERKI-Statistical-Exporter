package ru.nsu.datagen.importer;

import com.opencsv.CSVReader;
import com.opencsv.CSVReaderBuilder;
import com.opencsv.exceptions.CsvException;
import lombok.extern.slf4j.Slf4j;

import java.io.*;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.List;

@Slf4j
public class Importer {
    static public List<String[]> startImport(
            String schemasScriptPath, String statisticDataPath, Connection conn
    ) throws ImporterException {
        try {
            Statement statement = conn.createStatement();
            importSchemas(schemasScriptPath, statement);
            return importStatistic(statisticDataPath, conn, statement);
        } catch (ImporterException e) {
            log.error("Importer exception occurred.", e);
            throw e;
        } catch (SQLException e) {
            log.error("SQL exception occurred during import.", e);
            throw new RuntimeException(e);
        }
    }

    static private List<String[]> importStatistic(String path, Connection conn, Statement statement) {
        if (!new File(path).exists()) {
            throw new ImporterException("There is no import statistic file " + path );
        }
        try {
//            System.out.println(new File(path).exists());
            FileReader filereader = new FileReader(path);
            // CSVParser parser = new CSVParserBuilder().withSeparator('|').build();
            CSVReader csvReader = new CSVReaderBuilder(filereader)
                    .withSkipLines(1)
                    // .withCSVParser(parser)
                    .build();

            return csvReader.readAll();
        } catch (RuntimeException | IOException e) {
            log.error("Cannot import statistic from CSV file.", e);
            throw new ImporterException("Cannot import statistic from CSV file.");
        } catch (CsvException e) {
            throw new RuntimeException(e);
        }
    }

    static private void importSchemas(String path, Statement statement) throws ImporterException {
        if (!new File(path).exists()) {
            throw new ImporterException("There is no import schemas script " + path);
        }
        try {
            executeSqlScript(path, statement);
        } catch (Exception e) {
            throw new ImporterException("Cannot import schemas.");
        }
    }

    static private void executeSqlScript(String path, Statement statement) throws Exception {
        try {
            BufferedReader br = new BufferedReader(new FileReader(path));

            // String Builder to build the query line by line.
            StringBuilder query = new StringBuilder();
            String line;

            while ((line = br.readLine()) != null) {

                if (line.trim().startsWith("--")) { continue; }

                // Append the line into the query string and add a space after that
                query.append(line).append(" ");

                if (line.trim().endsWith(";")) {
                    // System.out.println(query);
                    // Execute the Query
                    statement.execute(query.toString().trim());
                    // Empty the Query string to add new query from the file
                    query = new StringBuilder();
                }
            }

            log.debug("Script file {} executed", path);

            // Getting the ResultSet after executing the Script File
            ResultSet resultSet = statement.getResultSet();
            log.debug("Result set: {}", resultSet);
        }
        catch (IOException e) {
            log.error("Some troubles with IO ops.", e);
            throw new IOException("Some troubles with IO ops.");
        } catch (SQLException e) {
            log.error("Troubles with executing SQL script!", e);
            throw new SQLException("Troubles with executing SQL script!");
        }
    }
}
