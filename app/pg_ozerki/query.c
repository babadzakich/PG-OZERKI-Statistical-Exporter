#include "pg_ozerki.h"

QueryDependencies* deps = NULL;



void
add_table(QueryDependencies *deps, const char *name)
{
    if (deps->tableCount == 0) {
        deps->tableNames = (char**)pg_malloc0(sizeof(char *));
    } else {
        deps->tableNames = repalloc(
            deps->tableNames,
            sizeof(char *) * (deps->tableCount + 1)
        );
    }
    deps->tableNames[deps->tableCount] = pstrdup(name);
    deps->tableCount++;
}


void add_schema_to_deps(QueryDependencies *deps, const char *schema) {

    for (int i = 0; i < deps->schemaCount; i++) {
        if (strcmp(deps->schemas[i], schema) == 0) {
            return;
        }
    }
    

    if (deps->schemaCount == 0) {
        deps->schemas = (char**)palloc(sizeof(char*));
    } else {
        deps->schemas = (char**)repalloc(deps->schemas, (deps->schemaCount + 1) * sizeof(char*));
    }
    deps->schemas[deps->schemaCount] = pstrdup(schema);
    deps->schemaCount++;
}

QueryDependencies* InitQueryDependencies() {
    QueryDependencies* deps = (QueryDependencies*)pg_malloc0(sizeof(QueryDependencies));
    deps->schemaCount = 0;
    deps->schemas = NULL;
    deps->tableCount = 0;
    deps->tableNames = NULL;
    deps->viewOids = NULL;
    deps->viewCount = 0;
    deps->been_analyzed = false;
    return deps;
}

QueryDependencies* extract_tables_from_query_text(PGconn* conn, const char *query, QueryDependencies* deps) {


    PQExpBuffer explain_query;
    PGresult* res;

    char* yaml_plan;

    

    explain_query = createPQExpBuffer();

    appendPQExpBufferStr(explain_query, "EXPLAIN (FORMAT YAML, VERBOSE) ");
    appendPQExpBufferStr(explain_query, query);
    res = PQexec(conn, explain_query->data);
    pg_log_debug("\n%s",  explain_query->data);
    ExecStatusType res_status = PQresultStatus(res);

    if (res_status != PGRES_TUPLES_OK) {
        pg_log_error("Explain query for dependencies has failed with error: %s", PQresultErrorMessage(res));
        PQclear(res);
        destroyPQExpBuffer(explain_query);
        exit(1);
    }
    
    yaml_plan = (PQgetvalue(res, 0, 0));


    const char *p = yaml_plan;


    while ((p = strstr(p, "Relation Name:")) != NULL)
    {
        const char *rel_start, *rel_end;
        const char *schema_p, *schema_start, *schema_end;
        char *relname;
        char *schemaname;
        char *fullname;

        rel_start = strchr(p, '"');
        if (!rel_start)
            break;
        rel_start++;

        rel_end = strchr(rel_start, '"');
        if (!rel_end)
            break;

        relname = pnstrdup(rel_start, rel_end - rel_start);

        schema_p = strstr(rel_end, "Schema:");
        schemaname = NULL;

        if (schema_p)
        {
            schema_start = strchr(schema_p, '"');
            if (schema_start)
            {
                schema_start++;
                schema_end = strchr(schema_start, '"');
                if (schema_end)
                    schemaname = pnstrdup(schema_start,
                                          schema_end - schema_start);
            }
        }

        if (!schemaname)
            schemaname = pstrdup("public");

        add_schema_to_deps(deps, schemaname);
        fullname = psprintf("%s.%s", schemaname, relname);
        add_table(deps, fullname);

        
        p = rel_end + 1;

        pfree(fullname);
        pfree(relname);
        pfree(schemaname);
    }

    destroyPQExpBuffer(explain_query);
    PQclear(res);

    return deps;
}




