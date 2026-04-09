#include "pg_ozerki.h"

#define COL_NUM 3

#define BUF_SIZE 1024



void export_constraints(PGconn* conn, const char* export_filename) {
    FILE* fp = fopen(export_filename, "w");

    if (!fp) {
        pg_log_error("Could not open file for stats export");
        return;
    }
    PQExpBuffer constrQuery;
    constrQuery = createPQExpBuffer();
    
    appendPQExpBuffer(constrQuery,
        "SELECT " 
"            c.conname AS constraint_name, "
"            CASE c.contype "
"                WHEN 'p' THEN 'PK' "
"                WHEN 'u' THEN 'UNIQUE' "
"                WHEN 'f' THEN 'FK' "
"            END AS type, "
"            string_agg(n.nspname || '.' || t.relname || '.' || a.attname, ', ' ORDER BY u.ord) AS columns "
"        FROM pg_constraint c "
"        JOIN pg_class t ON c.conrelid = t.oid "
"        JOIN pg_namespace n ON n.oid = t.relnamespace "
"        CROSS JOIN LATERAL unnest(c.conkey) WITH ORDINALITY AS u(attnum, ord) "
"        JOIN pg_attribute a ON a.attrelid = t.oid AND a.attnum = u.attnum "
"        WHERE c.contype IN ('p', 'u', 'f') "
"        AND n.nspname NOT IN ('pg_catalog', 'information_schema') "
"        GROUP BY n.nspname, t.relname, c.conname, c.contype "
        "ORDER BY n.nspname, t.relname, type"
    );

    PQExpBuffer cop_buf = createPQExpBuffer();
    appendPQExpBuffer(cop_buf, "COPY ( ");
    appendPQExpBuffer(cop_buf, constrQuery->data);
    appendPQExpBuffer(cop_buf," ) TO STDOUT WITH (FORMAT CSV, HEADER, NULL 'NULL')");
    PGresult* cop_res = PQexec(conn, cop_buf->data);

    ExecStatusType cop_res_status = PQresultStatus(cop_res);

    
    if (cop_res_status != PGRES_COPY_OUT) {
        pg_log_error("Constraints query has failed with result: %s", PQresultErrorMessage(cop_res));
        destroyPQExpBuffer(cop_buf);
        destroyPQExpBuffer(constrQuery);
        PQclear(cop_res);
        fclose(fp);
        return;
    }
    char* out_buf;
    int len;

    while ((len = PQgetCopyData(conn, &out_buf, 0)) > 0) {
        fwrite(out_buf, 1, len, fp);
        PQfreemem(out_buf);
    }
    fclose(fp);
    PQclear(cop_res);
    destroyPQExpBuffer(cop_buf);
    destroyPQExpBuffer(constrQuery);

}
