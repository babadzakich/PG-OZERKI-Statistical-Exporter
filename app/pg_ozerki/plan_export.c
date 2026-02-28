#include "pg_ozerki.h"

void get_explain(PGconn* conn, const char *query, char* filename, bool analyze) {


    FILE* fout = fopen(filename, "w");

    if (!fout) {
        pg_log_error("could not open the explain file");
        return;
    }
    PQExpBuffer explain_query;
    PGresult* res;
    PGresult* sp;
    char* json_plan;
    sp = PQexec(conn, "SHOW search_path");
    pg_log_debug("explain search path = %s", PQgetvalue(sp, 0, 0));
    explain_query = createPQExpBuffer();


    appendPQExpBufferStr(explain_query, "EXPLAIN (FORMAT JSON, VERBOSE");
    if (analyze) {
        appendPQExpBuffer(explain_query, ", ANALYZE");
    }
    appendPQExpBufferStr(explain_query, ")");
    appendPQExpBufferStr(explain_query, query);
    res = PQexec(conn, explain_query->data);
    ExecStatusType res_status = PQresultStatus(res); 
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        pg_log_error("Explain query has been executed with status: %s" ,PQresultErrorMessage(res));
        destroyPQExpBuffer(explain_query);
        PQclear(res);
        fclose(fout);
        return;
    }

    json_plan = (PQgetvalue(res, 0, 0));

    int length = strlen(json_plan);

    fwrite(json_plan, 1, length, fout);
    fclose(fout);

    PQclear(res);
    destroyPQExpBuffer(explain_query);
}

