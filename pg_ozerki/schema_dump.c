#include "postgres.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "catalog/pg_class.h"
#include "catalog/pg_class_d.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_attrdef.h"
#include "commands/defrem.h"
#include "nodes/pg_list.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "utils/fmgroids.h"
#include "access/htup_details.h"
#include "access/sysattr.h"
#include "lib/stringinfo.h"
#include "storage/fd.h"
#include "catalog/pg_proc.h"
#include "utils/ruleutils.h"
#include "commands/comment.h"
#include "access/genam.h"
#include "catalog/pg_attrdef_d.h"
#include "executor/spi.h"
#include "access/table.h"
#include "ddl.h"
#include "ozerki_utils.h"
#include "ddl_query.h"

PG_MODULE_MAGIC;



PG_FUNCTION_INFO_V1(dump_schema);
PG_FUNCTION_INFO_V1(dump_schema_by_query);
PG_FUNCTION_INFO_V1(dump_statistic_by_query);
PG_FUNCTION_INFO_V1(export_query_plan);

Datum
dump_schema(PG_FUNCTION_ARGS)
{
    int spi;

    StringInfoData buf;
    
    initStringInfo(&buf);
    
    appendStringInfo(&buf, "--PostgreSQL database schema dump by PG_OZERKI\n");
    appendStringInfo(&buf, "SET statement_timeout = 0;\n");
    appendStringInfo(&buf, "SET lock_timeout = 0;\n");
    appendStringInfo(&buf, "SET idle_in_transaction_session_timeout = 0;\n");
    appendStringInfo(&buf, "SET client_encoding = 'UTF8';\n");
    appendStringInfo(&buf, "SET standard_conforming_strings = on;\n");
    //appendStringInfo(&buf, "SELECT pg_catalog.set_config('search_path', '', false);\n\n");
    
    if ((spi = SPI_connect()) == SPI_OK_CONNECT){
        
        generate_planner_settings_ddl(&buf);

        generate_schemas_ddl(&buf);

        generate_functions_ddl(&buf);

        generate_extensions_ddl(&buf);
    
        generate_tables_ddl(&buf);

        generate_sequences_ddl(&buf);
        
        generate_views_ddl(&buf);
        
        generate_indexes_ddl(&buf);

        generate_constraints_ddl(&buf);

        SPI_execute("RESET search_path", false, 0);

    }
        
    
    
    elog(LOG, "%s\n\n", buf.data);
    
    text* ret = cstring_to_text(buf.data);
    //SPI_finish();
    PG_RETURN_TEXT_P(ret);
}

Datum
dump_schema_by_query(PG_FUNCTION_ARGS)
{
    int spi;
    
    StringInfoData buf;
    
    initStringInfo(&buf);
    
    appendStringInfo(&buf, "--PostgreSQL database schema dump by PG_OZERKI\n");
    appendStringInfo(&buf, "SET statement_timeout = 0;\n");
    appendStringInfo(&buf, "SET lock_timeout = 0;\n");
    appendStringInfo(&buf, "SET idle_in_transaction_session_timeout = 0;\n");
    appendStringInfo(&buf, "SET client_encoding = 'UTF8';\n");
    appendStringInfo(&buf, "SET standard_conforming_strings = on;\n");
    //appendStringInfo(&buf, "SELECT pg_catalog.set_config('search_path', '', false);\n\n");
    
    char* query = NULL;
    
    QueryDependencies* deps = NULL;
    if (PG_NARGS() > 0 && !PG_ARGISNULL(0)) {
        
        query = text_to_cstring(PG_GETARG_TEXT_PP(0));
        if (query && strlen(query) > 0) {
            if ((spi = SPI_connect()) == SPI_OK_CONNECT){
                deps = analyze_query_dependencies(query);
                elog(LOG, "\n\nDEPS ANALIZED\n\n");
                appendStringInfo(&buf, "-- Schema extracted from query: %s\n\n", query);
            }
        }
    }

    if (1){
        

        generate_planner_settings_ddl(&buf);

        generate_schemas_ddl_query(&buf, deps);

        generate_functions_ddl_query(&buf, deps);

        generate_extensions_ddl_query(&buf, deps);
    
        generate_tables_ddl_query(&buf, deps);

        generate_sequences_ddl_query(&buf, deps);
        
        generate_views_ddl_query(&buf, deps);
        
        generate_indexes_ddl_query(&buf, deps);

        generate_constraints_ddl_query(&buf, deps);

        SPI_execute("RESET search_path", false, 0);

    }
        
    
    
    elog(LOG, "%s\n\n", buf.data);
    
    text* ret = cstring_to_text(buf.data);
    //SPI_finish();
    PG_RETURN_TEXT_P(ret);
}



Datum 
dump_statistic_by_query(PG_FUNCTION_ARGS) 
{
    
    int spi;
    
    
    char* query = NULL;
    StringInfoData statQuery;
    initStringInfo(&statQuery);
    QueryDependencies* deps = NULL;
    if (PG_NARGS() > 0 && !PG_ARGISNULL(0)) {
        
        query = text_to_cstring(PG_GETARG_TEXT_PP(0));
        if (query && strlen(query) > 0) {
            if ((spi = SPI_connect()) == SPI_OK_CONNECT){
                deps = analyze_query_dependencies(query);
                elog(LOG, "\n\nDEPS ANALIZED\n\n");
            }
        }
    }
    
    //SPI_finish();
    
    
    appendStringInfoString(&statQuery,
        "WITH table_counts AS ("
    "    SELECT "
    "        n.nspname AS schemaname, "
    "        c.relname AS table_name, "
    "        c.reltuples::bigint AS row_count "
    "    FROM pg_class c "
    "    JOIN pg_namespace n ON n.oid = c.relnamespace "
    "    WHERE c.relkind = 'r' "
    "      AND n.nspname NOT IN ('pg_catalog', 'information_schema') "
    "), "
    "fk_constraints AS ("
    "    SELECT"
    "        con.conrelid, "
    "        con.confrelid, "
    "        con.conname, "
    "        con.conkey, "
    "        con.confkey, "
    "        a.attname AS column_name, "
    "        a.attnum AS column_num, "
    "        con_tbl.relname AS table_name, "
    "        con_tbl_ns.nspname AS table_schema, "
    "        conf_tbl.relname AS referenced_table, "
    "        conf_tbl_ns.nspname AS referenced_schema, "
    "        (SELECT attname FROM pg_attribute "
    "         WHERE attrelid = con.confrelid AND attnum = con.confkey[1]) AS referenced_column "
    "    FROM pg_constraint con "
    "    JOIN pg_attribute a ON a.attrelid = con.conrelid AND a.attnum = con.conkey[1] "
    "    JOIN pg_class con_tbl ON con_tbl.oid = con.conrelid "
    "    JOIN pg_namespace con_tbl_ns ON con_tbl_ns.oid = con_tbl.relnamespace "
    "    JOIN pg_class conf_tbl ON conf_tbl.oid = con.confrelid "
    "    JOIN pg_namespace conf_tbl_ns ON conf_tbl_ns.oid = conf_tbl.relnamespace "
    "    WHERE con.contype = 'f' "
    "), "
    "column_constraints AS ("
    "    SELECT "
    "        a.attrelid, "
    "        a.attname, "
    "        a.attnum, "
    "        BOOL_OR(c.contype = 'p') AS is_pk, "
    "        BOOL_OR(c.contype = 'u') AS is_unique_constraint, "
    "        BOOL_OR(c.contype = 'c') AS is_check_constraint, "
    "        BOOL_OR(EXISTS ("
    "            SELECT 1 FROM pg_index i "
    "            WHERE i.indrelid = a.attrelid "
    "            AND a.attnum = ANY(i.indkey) "
    "            AND i.indisunique = true "
    "            AND i.indisprimary = false "
    "        )) AS is_unique_index "
    "    FROM pg_attribute a "
    "    LEFT JOIN pg_constraint c ON c.conrelid = a.attrelid AND a.attnum = ANY(c.conkey) "
    "    WHERE a.attnum > 0 AND NOT a.attisdropped "
    "    GROUP BY a.attrelid, a.attname, a.attnum "
    "), "
    "relation_types AS ("
    "    SELECT "
    "        fk.table_schema, "
    "        fk.table_name, "
    "        fk.column_name, "
    "        fk.referenced_schema, "
    "        fk.referenced_table, "
    "        fk.referenced_column, "
    "        CASE "
    "            WHEN cc.is_pk OR cc.is_unique_constraint OR cc.is_unique_index THEN"
    
    "                CASE WHEN EXISTS ("
    "                    SELECT 1 FROM column_constraints ref_cc "
    "                    JOIN pg_class ref_tbl ON ref_tbl.oid = ref_cc.attrelid "
    "                    JOIN pg_namespace ref_ns ON ref_ns.oid = ref_tbl.relnamespace "
    "                    WHERE ref_ns.nspname = fk.referenced_schema "
    "                    AND ref_tbl.relname = fk.referenced_table "
    "                    AND ref_cc.attname = fk.referenced_column "
    "                    AND (ref_cc.is_pk OR ref_cc.is_unique_constraint) "
    "                ) THEN 'ONE_TO_ONE' ELSE 'ONE_TO_ONE' END"
    "            ELSE 'ONE_TO_MANY' "
    "        END AS relation_type "
    "    FROM fk_constraints fk "
    "    JOIN column_constraints cc ON cc.attrelid = fk.conrelid AND cc.attname = fk.column_name"
    "), "
    "column_stats AS ("
    "    SELECT "
    "        n.nspname AS table_schema, "
    "        c.relname AS table_name, "
    "        a.attname AS column_name, "
    "        pg_catalog.format_type(a.atttypid, a.atttypmod) AS data_type, "
    "        a.attnum AS column_number, "
    "        CASE "
    "            WHEN a.atttypid IN (1042, 1043, 25) THEN"
    "                CASE "
    "                    WHEN a.atttypmod = -1 THEN -1"
    "                    ELSE a.atttypmod - 4"
    "                END"
    "            ELSE -1"
    "        END AS max_length,"
    "        (SELECT COUNT(*) FROM pg_constraint "
    "         WHERE conrelid = c.oid AND contype = 'p' AND a.attnum = ANY(conkey)) AS is_primary_key,"
    "        (SELECT COUNT(*) FROM pg_constraint "
    "         WHERE conrelid = c.oid AND contype = 'f' AND a.attnum = ANY(conkey)) AS is_foreign_key,"
    "        (SELECT COUNT(*) FROM pg_constraint "
    "         WHERE conrelid = c.oid AND contype = 'u' AND a.attnum = ANY(conkey)) AS is_unique,"
    "        (SELECT COUNT(*) FROM pg_constraint "
    "         WHERE conrelid = c.oid AND contype = 'c' AND a.attnum = ANY(conkey)) AS is_check,"
    "        (SELECT COUNT(*) FROM pg_index "
    "         WHERE indrelid = c.oid AND indisunique = true AND indisprimary = false "
    "         AND a.attnum = ANY(indkey)) AS is_unique_index "
    "    FROM pg_attribute a "
    "    JOIN pg_class c ON a.attrelid = c.oid "
    "    JOIN pg_namespace n ON c.relnamespace = n.oid "
    "    WHERE a.attnum > 0 "
    "      AND NOT a.attisdropped "
    "      AND c.relkind = 'r' "
    "      AND n.nspname NOT IN ('pg_catalog', 'information_schema') "
    "      AND c.relname IN ("
    );
    
    char* token;
    for (int i = 0; i < deps->tableCount; i++) {
            if (i > 0) appendStringInfoString(&statQuery, ", ");
            char *name = pstrdup(deps->tableNames[i]);
            token = strtok(name, ".");
            token = strtok(NULL, ".");
            appendStringInfo(&statQuery, "'%s'", token);
    }
    
    appendStringInfoString(&statQuery,
        ")"
        ") "
        " SELECT "
        "    cs.table_schema, "
        "    cs.table_name, "
        "    cs.column_name, "
        "    cs.data_type, "
        "    tc.row_count, "
        "    CASE "
        "        WHEN tc.row_count = 0 THEN 0"
        "        ELSE ROUND("
        "            (s.null_frac * 100)::numeric, "
        "            2"
        "        )"
        "    END AS null_percent,"
        "    TRIM("
        "        CASE WHEN cs.is_primary_key > 0 THEN 'PK ' ELSE '' END ||"
        "        CASE WHEN cs.is_foreign_key > 0 THEN 'FK ' ELSE '' END ||"
        "        CASE WHEN cs.is_unique > 0 OR cs.is_unique_index > 0 THEN 'UNIQUE ' ELSE '' END ||"
        "        CASE WHEN cs.is_check > 0 THEN 'CHECK' ELSE '' END"
        "    ) AS modifiers, "
        "    cs.max_length, "
        "    rt.relation_type, "
        "    rt.referenced_table, "
        "    rt.referenced_column, "
        "    s.most_common_vals AS mcv, "
        "    s.most_common_freqs AS mcv_frequencies, " 
        "    s.avg_width AS avg_column_width_bytes, "
        "    s.n_distinct AS ndistinct, "
        "    s.histogram_bounds as hbounds "
        "FROM column_stats cs "
        "JOIN table_counts tc ON cs.table_schema = tc.schemaname AND cs.table_name = tc.table_name "
        "LEFT JOIN pg_stats s ON s.schemaname = cs.table_schema "
        "                     AND s.tablename = cs.table_name "
        "                     AND s.attname = cs.column_name "
        "LEFT JOIN relation_types rt ON rt.table_schema = cs.table_schema "
        "                           AND rt.table_name = cs.table_name "
        "                           AND rt.column_name = cs.column_name "
        "ORDER BY "
        "    cs.table_schema, "
        "    cs.table_name, "
        "    cs.column_number"

    );
    char* resQuery = statQuery.data;
  
    SPI_finish();
    FuncCallContext *funcctx;
    int call_cntr;
    int max_calls;
    AttInMetadata *attinmeta;
    TupleDesc tupdesc;
    int spi_ret;
    if (SRF_IS_FIRSTCALL())
    {
        MemoryContext oldcontext;
        funcctx = SRF_FIRSTCALL_INIT();
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                            errmsg("function returning record called in context that cannot accept type record")));    
        if (SPI_connect() != SPI_OK_CONNECT)
            ereport(ERROR, (errmsg("SPI_connect failed")));

        spi_ret = SPI_execute(resQuery, true, 0);  

        if (spi_ret < 0)
            ereport(ERROR,
                    (errmsg("SPI_execute failed with code %d", spi_ret),
                     errdetail("Query: %s", resQuery)));

        

        funcctx->user_fctx = (void*) SPI_tuptable;
        funcctx->max_calls = SPI_processed;
        
        funcctx->tuple_desc = BlessTupleDesc(tupdesc);
        attinmeta = TupleDescGetAttInMetadata(tupdesc);
        funcctx->attinmeta = attinmeta; 
        MemoryContextSwitchTo(oldcontext);
    }

    funcctx = SRF_PERCALL_SETUP();
    call_cntr = funcctx->call_cntr;
    max_calls = funcctx->max_calls;
    attinmeta = funcctx->attinmeta;
    elog(LOG, "\n\n call cntr =  %d \n\n ", funcctx->call_cntr);
    if (funcctx->call_cntr < funcctx->max_calls)
    {   
         
        char* values[16];
        bool  nulls[16];
        int i;
        
        memset(values, 0, sizeof(values));
        memset(nulls,  true, sizeof(nulls));  // по умолчанию NULL
        HeapTuple current_tuple = SPI_tuptable->vals[funcctx->call_cntr];
        for (int i = 0; i < 16; i++) {
            values[i] = SPI_getvalue(current_tuple, SPI_tuptable->tupdesc, i+1);
            //elog(LOG, "\n %s \n", values[i]);
        }
        
        // Остальные поля остаются NULL

        HeapTuple tuple;
        Datum     result;
        
        tuple = BuildTupleFromCStrings(attinmeta, values);
        
        result = HeapTupleGetDatum(tuple);

        //funcctx->call_cntr++;
        
        SRF_RETURN_NEXT(funcctx, result);
    }
    else
    {
        
        if (funcctx->user_fctx)
        {
            
            funcctx->user_fctx = NULL;
        }
        //SPI_finish();
        SRF_RETURN_DONE(funcctx);
    }
}

Datum
export_query_plan(PG_FUNCTION_ARGS) {
    int spi;
    
    bool analyze = false;
    char* query = NULL;
    char* plan;
    SPITupleTable saved;
    StringInfoData explainQuery;
    initStringInfo(&explainQuery);
    if (PG_NARGS() > 0 && !PG_ARGISNULL(0) && !PG_ARGISNULL(1)) {
        analyze = PG_GETARG_BOOL(1);
        query = text_to_cstring(PG_GETARG_TEXT_PP(0));
        if (query && strlen(query) > 0) {
            if ((spi = SPI_connect()) == SPI_OK_CONNECT){
                elog(LOG, "Starting plan export: %s", query);
                
                if (analyze) {
                appendStringInfoString(&explainQuery,
                    "EXPLAIN (FORMAT YAML, VERBOSE, ANALYZE) ");
                } else {
                    appendStringInfoString(&explainQuery,
                    "EXPLAIN (FORMAT YAML, VERBOSE) ");
                }
                appendStringInfo(&explainQuery, query);
                    
                char* explainQueryCopy = explainQuery.data;
                
                int ret = SPI_execute(explainQueryCopy, false, 0);
                saved = *SPI_tuptable;
                if (ret == SPI_OK_UTILITY && SPI_processed > 0) {
                    TupleDesc tupdesc = (&saved)->tupdesc;
                    HeapTuple tuple = (&saved)->vals[0];
                    plan = SPI_getvalue(tuple, tupdesc, 1);
                    
                }
                char* res = pstrdup(plan);
                elog(LOG, "\n\n RES = %s \n\n", res);
                //SPI_finish();
                elog(LOG, "\n\n RES = %s \n\n", res);
                PG_RETURN_TEXT_P(cstring_to_text(res));
                
            }
        }
    }
}