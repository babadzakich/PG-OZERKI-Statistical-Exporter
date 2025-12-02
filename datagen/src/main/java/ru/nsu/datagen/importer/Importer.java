package ru.nsu.datagen.importer;

import com.opencsv.CSVParser;
import com.opencsv.CSVParserBuilder;
import com.opencsv.CSVReader;
import com.opencsv.CSVReaderBuilder;
import com.opencsv.exceptions.CsvException;

import javax.swing.plaf.nimbus.State;
import java.io.*;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.List;


public class Importer {
    static public List<String[]> startImport(
            String schemasScriptPath, String statisticDataPath, Connection conn
    ) throws ImporterException {
        try {
            Statement statement = conn.createStatement();
            importSchemas(schemasScriptPath, statement);
            return importStatistic(statisticDataPath, conn, statement);
        } catch (ImporterException e) {
            throw e;
        } catch (SQLException e) {
            throw new RuntimeException(e);
        }
    }

    static private List<String[]> importStatistic(String path, Connection conn, Statement statement) {
        if (new File(path).exists() == false) {
            throw new ImporterException("There is no import statistic file " + path );
        }
        try {
            System.out.println(new File(path).exists());
            FileReader filereader = new FileReader(path);
            // CSVParser parser = new CSVParserBuilder().withSeparator('|').build();
            CSVReader csvReader = new CSVReaderBuilder(filereader)
                    .withSkipLines(1)
                    // .withCSVParser(parser)
                    .build();
            List<String[]> allData = csvReader.readAll();

            return allData;
        } catch (RuntimeException | IOException e) {
            System.err.println(e);
            throw new ImporterException("Cannot import statistic from CSV file.");
        } catch (CsvException e) {
            throw new RuntimeException(e);
        }
    }

    static private void importSchemas(String path, Statement statement) throws ImporterException {
        if (new File(path).exists() == false) {
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
                    System.out.println(query);
                    // Execute the Query
                    statement.execute(query.toString().trim());
                    // Empty the Query string to add new query from the file
                    query = new StringBuilder();
                }
            }

            System.out.println("Script file " + path + " executed");

            // Getting the ResultSet after executing the Script File
            ResultSet resultSet = statement.getResultSet();
            System.out.println("Result set: " + resultSet);
        }
        catch (IOException e) {
            System.err.println(e);
            throw new IOException("Some troubles with IO ops.");
        } catch (SQLException e) {
            System.err.println(e);
            throw new SQLException("Troubles with executing SQL script!");
        }
    }
}
