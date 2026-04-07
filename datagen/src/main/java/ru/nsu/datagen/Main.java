package ru.nsu.datagen;

import com.beust.jcommander.JCommander;
import lombok.extern.slf4j.Slf4j;
import org.yaml.snakeyaml.Yaml;
import ru.nsu.datagen.argValidation.Arguments;
import ru.nsu.datagen.pipeline.Pipeline;

import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.sql.SQLException;

@Slf4j
public class Main {
    public static void main(String ... argv) throws SQLException {
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

         Pipeline.startPipeline(
                 args.host, args.port, args.dbname, args.user, args.password,
                 args.schemaPath, args.statPath, args.IOthreads
         );
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

        if (!missing.isEmpty()) {
            throw new IllegalArgumentException("Missing required arguments: " + missing);
        }
    }
}