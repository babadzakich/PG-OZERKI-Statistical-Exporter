package ru.nsu.datagen;

import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.sql.SQLException;

import org.yaml.snakeyaml.Yaml;

import com.beust.jcommander.JCommander;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.argValidation.Arguments;
import ru.nsu.datagen.pipeline.Pipeline;

/**
 * Точка входа в приложение.
 *
 * <p>Разбирает аргументы командной строки (или YAML-конфиг) и запускает {@link Pipeline}.
 * Аргументы CLI всегда имеют приоритет над значениями из конфига.
 *
 * <p>Обязательные аргументы: {@code -host}, {@code -port}, {@code -dbname}, {@code -user},
 * {@code -passwd}, {@code -schema}, {@code -stat}, {@code -indexes}, {@code -constraints}.
 * Все они могут быть заданы через {@code -config <yaml-файл>}.
 */
@Slf4j
public class Main {
    /**
     * Парсит аргументы, опционально мёржит YAML-конфиг, проверяет полноту и запускает
     * {@link Pipeline#startPipeline}.
     *
     * @param argv аргументы командной строки
     * @throws SQLException если пайплайн столкнулся с ошибкой БД
     * @throws IOException  если конфиг или входной файл не читается
     */
    public static void main(String ... argv) throws SQLException, IOException {
         Arguments args = new Arguments();

         JCommander.newBuilder().addObject(args).build().parse(argv);

         if (args.help) {
             JCommander.newBuilder().addObject(args).build().usage();
             return;
         }

         try {
            loadConfig(args);
            validateArgs(args);
         } catch (IOException e) {
            log.error("Error loading config: ", e);
            return;
         } catch (IllegalArgumentException e) {
            log.error("Argument validation failed: ", e);
            JCommander.newBuilder().addObject(args).build().usage();
            return;
         }

         Pipeline.startPipeline(args);
    }

    private static void loadConfig(Arguments args) throws IOException {
        if (args.configPath == null) {
            return;
        }
        try (InputStream inputStream = new FileInputStream(args.configPath)) {
            Yaml yaml = new Yaml();
            Arguments configArgs = yaml.loadAs(inputStream, Arguments.class);

            if (args.host == null) args.host = configArgs.host;
            if (args.port == null) args.port = configArgs.port;
            if (args.dbname == null) args.dbname = configArgs.dbname;
            if (args.user == null) args.user = configArgs.user;
            if (args.password == null) args.password = configArgs.password;
            if (args.schemaPath == null) args.schemaPath = configArgs.schemaPath;
            if (args.statPath == null) args.statPath = configArgs.statPath;
            if (args.indexFile == null) args.indexFile = configArgs.indexFile;
            if (args.constraintFile == null) args.constraintFile = configArgs.constraintFile;
            args.batchSize = configArgs.batchSize != null ? configArgs.batchSize : args.batchSize;
            args.generationThreadPoolSize = configArgs.generationThreadPoolSize != null ? configArgs.generationThreadPoolSize : args.generationThreadPoolSize;
            args.globStoreThreads = configArgs.globStoreThreads != null ? Math.min(Runtime.getRuntime().availableProcessors(), configArgs.globStoreThreads) :
                    Math.min(Runtime.getRuntime().availableProcessors(), args.globStoreThreads);
            args.tableStoreThreads = configArgs.tableStoreThreads != null ? configArgs.tableStoreThreads : args.tableStoreThreads;
        }
    }

    private static void validateArgs(Arguments args) {
        StringBuilder missing = new StringBuilder();
        if (args.host == null) missing.append("-host ");
        if (args.port == null) missing.append("-port ");
        if (args.dbname == null) missing.append("-dbname ");
        if (args.user == null) missing.append("-user ");
        if (args.password == null) missing.append("-passwd ");
        if (args.schemaPath == null) missing.append("-schema ");
        if (args.statPath == null) missing.append("-stat ");
        if (args.indexFile == null) missing.append("-indexes ");
        if (args.constraintFile == null) missing.append("-constraints ");

        if (!missing.isEmpty()) {
            throw new IllegalArgumentException("Missing required arguments: " + missing);
        }
    }
}