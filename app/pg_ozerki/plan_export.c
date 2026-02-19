#include "pg_ozerki.h"

void get_explain(PGconn* conn, const char *query, char* filename, bool analyze) {


    FILE* fout = fopen(filename, "w");

    if (!fout) {
        pg_log_error("could not open the explain file");
        return;
    }
    PQExpBuffer explain_query;
    PGresult* res;

    char* yaml_plan;

    explain_query = createPQExpBuffer();


    appendPQExpBufferStr(explain_query, "EXPLAIN (FORMAT YAML, VERBOSE");
    if (analyze) {
        appendPQExpBuffer(explain_query, ", ANALYZE");
    }
    appendPQExpBufferStr(explain_query, ")");
    appendPQExpBufferStr(explain_query, query);
    res = PQexec(conn, explain_query->data);
    ExecStatusType res_status = PQresultStatus(res); 
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        pg_log_error("Explain query has been executed with status: %s" ,PQresStatus(res_status));
        destroyPQExpBuffer(explain_query);
        PQclear(res);
        fclose(fout);
        return;
    }

    yaml_plan = (PQgetvalue(res, 0, 0));

    int length = strlen(yaml_plan);

    fwrite(yaml_plan, 1, length, fout);
    fclose(fout);

    PQclear(res);
    destroyPQExpBuffer(explain_query);
}

