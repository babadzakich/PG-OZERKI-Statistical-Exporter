package ru.nsu.datagen;

import com.beust.jcommander.JCommander;
import ru.nsu.datagen.argValidation.Arguments;
import ru.nsu.datagen.pipeline.Pipeline;

import java.nio.channels.Pipe;
import java.sql.SQLException;


public class Main {
    public static void main(String ... argv) throws SQLException {
        Arguments args = new Arguments();

        // JCommander.newBuilder().addObject(args).build().parse(argv);
        // Pipeline.startPipeline(
        //         args.host, args.port, args.dbname, args.user, args.password,
        //         args.schemaScriptPath, args.statisticDataPath
        // );

        
        Pipeline.startPipeline(
                "localhost", 5432, "new", "kubicl",

                "postgres", "/home/kubicl/Рабочий стол/PG-OZERKI-Statistical-Exporter/datagen/src/main/java/ru/nsu/datagen/scripts/schema.sql", "/home/kubicl/Рабочий стол/PG-OZERKI-Statistical-Exporter/datagen/src/main/java/ru/nsu/datagen/scripts/data.csv");

        
    }
}