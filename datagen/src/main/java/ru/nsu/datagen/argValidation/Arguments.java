package ru.nsu.datagen.argValidation;

import com.beust.jcommander.Parameter;

public class Arguments {
    @Parameter(names = "-host", required = true)
    public String host;

    @Parameter(names = "-port", required = true)
    public int port;

    @Parameter(names = "-dbname", required = true)
    public String dbname;

    @Parameter(names = "-user", required = true)
    public String user;

    @Parameter(names = "-passwd", required = true, password = true)
    public String password;

    @Parameter(names = "-schema", required = true, validateWith = FilePath.class)
    public String schemaScriptPath;

    @Parameter(names = "-stat", required = true, validateWith = FilePath.class)
    public String statisticDataPath;

}
