#include "pg_ozerki.h"

void get_explain(PGconn* conn, const char *query, char* filename) {


    FILE* fout = fopen(filename, "w");

    PQExpBuffer explain_query;
    PGresult* res;

    char* yaml_plan;

    explain_query = createPQExpBuffer();


    appendPQExpBufferStr(explain_query, "EXPLAIN (FORMAT YAML, VERBOSE) ");
    appendPQExpBufferStr(explain_query, query);
    res = PQexec(conn, explain_query->data);

    
    yaml_plan = (PQgetvalue(res, 0, 0));

    int length = strlen(yaml_plan);

    fwrite(yaml_plan, 1, length, fout);
    fclose(fout);
}

void get_explain_analyze(PGconn* conn, const char *query, char* filename) {

    FILE* fout = fopen(filename, "w");
    PQExpBuffer explain_query;
    PGresult* res;

    char* yaml_plan;

    explain_query = createPQExpBuffer();

    appendPQExpBufferStr(explain_query, "EXPLAIN (FORMAT YAML, VERBOSE, ANALYZE) ");
    appendPQExpBufferStr(explain_query, query);
    res = PQexec(conn, explain_query->data);

    
    yaml_plan = (PQgetvalue(res, 0, 0));

    int length = strlen(yaml_plan);

    fwrite(yaml_plan, 1, length, fout);

    fclose(fout);
}