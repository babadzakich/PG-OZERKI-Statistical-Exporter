package ru.nsu.datagen.argValidation;

import com.beust.jcommander.Parameter;

public class Arguments {
    @Parameter(names = "-host")
    public String host;

    @Parameter(names = "-port")
    public Integer port;

    @Parameter(names = "-dbname")
    public String dbname;

    @Parameter(names = "-user")
    public String user;

    @Parameter(names = "-passwd", password = true)
    public String password;

    @Parameter(names = "-schema", validateWith = FilePath.class)
    public String schemaPath;

    @Parameter(names = "-stat", validateWith = FilePath.class)
    public String statPath;

    @Parameter(names = "-config", validateWith = FilePath.class)
    public String configPath;

    @Parameter(names = "-batchSize")
    public Integer batchSize = 10000;

    @Parameter(names = "-generationThreadPoolSize")
    public Integer generationThreadPoolSize = 4;

    @Parameter(names = "-globStoreThreads")
    public Integer globStoreThreads = 1;

    @Parameter(names = "-tableStoreThreads")
    public Integer tableStoreThreads = 1;

    @Parameter(names = "-help", help = true)
    public boolean help;

    @Parameter(names = "-indexes")
    public String indexFile;

    @Parameter(names = "-constraints", validateWith=FilePath.class)
    public String constraintFile;
}
