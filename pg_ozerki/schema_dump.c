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

#include "ozerki_utils.h"
#include "ddl_query.h"

PG_MODULE_MAGIC;



PG_FUNCTION_INFO_V1(dump_schema);
PG_FUNCTION_INFO_V1(dump_statistic);
PG_FUNCTION_INFO_V1(export_query_plan);

Datum
dump_schema(PG_FUNCTION_ARGS)
{
    int spi;
    
    StringInfoData buf;
    text* ret;
    char* ret_schema;

    MemoryContext old_context;

    initStringInfo(&buf);
    
    old_context = CurrentMemoryContext;


    appendStringInfo(&buf, "--PostgreSQL database schema dump by PG_OZERKI\n");
    appendStringInfo(&buf, "SET statement_timeout = 0;\n");
    appendStringInfo(&buf, "SET lock_timeout = 0;\n");
    appendStringInfo(&buf, "SET idle_in_transaction_session_timeout = 0;\n");
    appendStringInfo(&buf, "SET client_encoding = 'UTF8';\n");
    appendStringInfo(&buf, "SET standard_conforming_strings = on;\n");
    //appendStringInfo(&buf, "SELECT pg_catalog.set_config('search_path', '', false);\n\n");
    
    char* query = NULL;
    
    QueryDependencies* deps = init_deps();
    if ((spi = SPI_connect()) != SPI_OK_CONNECT){
        ereport(ERROR, "SPI connection has failed with error: %s", SPI_result_code_string(spi));
        PG_RETURN_NULL();
    }
    if (PG_NARGS() > 0 && !PG_ARGISNULL(0)) {
        
        query = text_to_cstring(PG_GETARG_TEXT_PP(0));
        if (query && strlen(query) > 0) {
            
            analyze_query_dependencies(query, deps);
            elog(LOG, "\n\nDEPS ANALIZED\n\n");
            appendStringInfo(&buf, "-- Schema extracted from query: %s\n\n", query);
            
        }
    } 

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

        
    
    
    elog(LOG, "\n\n buf len = %d", buf.len);
    ret_schema = (char*) MemoryContextAlloc(old_context, buf.len + 1);
    memcpy(ret_schema, buf.data, buf.len + 1);
    
    SPI_finish();
    ret = cstring_to_text(ret_schema);
    elog(LOG, "%s\n\n", ret_schema);
    PG_RETURN_TEXT_P(ret);
}




Datum 
dump_statistic(PG_FUNCTION_ARGS) 
{
    
    SPITupleTable* saved;
    uint64_t max_calls;
    FuncCallContext *funcctx;
    AttInMetadata *attinmeta;
    
    HeapTuple *tuples;
    int spi_ret;
    
    
    TupleDesc tupdesc;
    TupleDesc out_tupdesc;
    MemoryContext oldcontext;
    TupleDesc spi_tupdesc;     
    if (SRF_IS_FIRSTCALL())
    {
        int spi;
        MemoryContext upper_ctx;
        char* query = NULL;
        char* resQuery = NULL;
        StringInfoData statQuery;
        initStringInfo(&statQuery);
        QueryDependencies* deps = init_deps();
        upper_ctx = CurrentMemoryContext;
        if ((spi = SPI_connect()) != SPI_OK_CONNECT){
            ereport(ERROR, "SPI connection has failed with error: %s", SPI_result_code_string(spi));
            PG_RETURN_NULL();
        }
        SPI_execute(" SET datestyle TO 'ISO' ", false, 0);
        SPI_execute(" SET intervalstyle to 'iso_8601' ", false, 0);
        
        if (PG_NARGS() > 0 && !PG_ARGISNULL(0)) {
            
            query = text_to_cstring(PG_GETARG_TEXT_PP(0));
            if (query && strlen(query) > 0) {
                
                analyze_query_dependencies(query, deps);
                elog(LOG, "\n\nDEPS ANALIZED\n\n");
                
            }
        }
        
        
        

        appendStringInfoString(&statQuery,
        "WITH table_counts AS ( "
        "    SELECT  "
        "        n.nspname AS schemaname, "
        "        c.relname AS table_name, "
        "        c.reltuples::bigint AS row_count "
        "    FROM pg_class c "
        "    JOIN pg_namespace n ON n.oid = c.relnamespace "
        "    WHERE c.relkind = 'r' "
        "      AND n.nspname NOT IN ('pg_catalog', 'information_schema') "
        "), "
        "column_flags AS ( "
        "    SELECT "
        "        a.attrelid, "
        "        a.attname, "
        "        a.attnum, "
        "        BOOL_OR(c.contype = 'p') AS is_pk, "
        "        BOOL_OR(c.contype = 'u') AS is_unique_con, "
        "        BOOL_OR(EXISTS ( "
        "            SELECT 1 FROM pg_index i  "
        "            WHERE i.indrelid = a.attrelid AND a.attnum = ANY(i.indkey)  "
        "            AND i.indisunique AND NOT i.indisprimary "
        "        )) AS is_unique_idx "
        "    FROM pg_attribute a "
        "    LEFT JOIN pg_constraint c ON c.conrelid = a.attrelid AND a.attnum = ANY(c.conkey) "
        "    WHERE a.attnum > 0 AND NOT a.attisdropped "
        "    GROUP BY a.attrelid, a.attname, a.attnum "
        "), "
        "fk_constraints AS ( "
        "    SELECT "
        "        con.conrelid, "
        "        a_src.attname AS src_column, "
        "        conf_ns.nspname || '.' || conf_tbl.relname || '.' || a_ref.attname AS target_path, "
        "        CASE  "
        "            WHEN (src_f.is_pk OR src_f.is_unique_con OR src_f.is_unique_idx)  "
        "            THEN 'ONE_TO_ONE' ELSE 'ONE_TO_MANY'  "
        "        END AS rel_type "
        "    FROM pg_constraint con "
        "    CROSS JOIN LATERAL unnest(con.conkey, con.confkey) AS pairs(src_num, ref_num) "
        "    JOIN pg_attribute a_src ON a_src.attrelid = con.conrelid AND a_src.attnum = pairs.src_num "
        "    JOIN pg_attribute a_ref ON a_ref.attrelid = con.confrelid AND a_ref.attnum = pairs.ref_num "
        "    JOIN pg_class conf_tbl ON conf_tbl.oid = con.confrelid "
        "    JOIN pg_namespace conf_ns ON conf_ns.oid = conf_tbl.relnamespace "
        "    JOIN column_flags src_f ON src_f.attrelid = con.conrelid AND src_f.attnum = pairs.src_num "
        "    WHERE con.contype = 'f' "
        "), "
        "aggregated_references AS ( "
        "    SELECT  "
        "        conrelid, "
        "        src_column, "
        "        string_agg(target_path, ', ') AS outcoming_references, "
        "        string_agg(rel_type, ', ') AS relation_types "
        "    FROM fk_constraints "
        "    GROUP BY conrelid, src_column "
        "), "
        "composite_unique_info AS ( "
        "    SELECT  "
        "        a.attrelid, "
        "        a.attname, "
        "        ( "
        "            SELECT string_agg(DISTINCT n.nspname || '.' || c.relname || '.' || a_other.attname, ', ') "
        "            FROM ( "
        "                SELECT unnest(conkey) as col_num, conrelid as rel_id "
        "                FROM pg_constraint  "
        "                WHERE contype IN ('u', 'p') AND array_length(conkey, 1) > 1 "
        "                UNION ALL "
        "                SELECT unnest(indkey) as col_num, indrelid as rel_id "
        "                FROM pg_index  "
        "                WHERE indisunique = true AND array_length(indkey, 1) > 1 "
        "            ) sub "
    "    JOIN pg_class c ON c.oid = sub.rel_id "
    "            JOIN pg_namespace n ON n.oid = c.relnamespace "
        "            JOIN pg_attribute a_other ON a_other.attrelid = sub.rel_id AND a_other.attnum = sub.col_num "
        "            WHERE sub.rel_id = a.attrelid  "
        "              AND a_other.attname <> a.attname "
        "              AND EXISTS ( "
        "                  SELECT 1 FROM ( "
        "                      SELECT conkey as keys, conrelid as rid FROM pg_constraint WHERE contype IN ('u', 'p') "
        "                      UNION ALL "
        "                      SELECT indkey as keys, indrelid as rid FROM pg_index WHERE indisunique = true "
        "                  ) check_sub  "
        "                  WHERE rid = a.attrelid AND a.attnum = ANY(keys) AND a_other.attnum = ANY(keys) "
        "              ) "
        "        ) as composite_unique_peers "
        "    FROM pg_attribute a "
        "    WHERE a.attnum > 0 AND NOT a.attisdropped "
        "), "
        "composite_fk_info AS ( "
        "    SELECT  "
        "        a.attrelid, "
        "        a.attname, "
        "        string_agg(DISTINCT n.nspname || '.' || c.relname || '.' || peer.attname, ', ') as peers "
        "    FROM pg_attribute a "
        " JOIN pg_class c ON c.oid = a.attrelid "
    " JOIN pg_namespace n ON n.oid = c.relnamespace "
        "    JOIN pg_constraint con ON con.conrelid = a.attrelid AND a.attnum = ANY(con.conkey) "
        "    JOIN pg_attribute peer ON peer.attrelid = a.attrelid AND peer.attnum = ANY(con.conkey) AND peer.attnum <> a.attnum "
        "    WHERE con.contype = 'f'  "
        "      AND array_length(con.conkey, 1) > 1 "
        "      AND a.attnum > 0  "
        "      AND NOT a.attisdropped "
        "    GROUP BY a.attrelid, a.attname "
        "), "
        "incoming_references_info AS ( "
        "    SELECT "
        "        con.confrelid AS target_table_oid, "
        "        a_target.attname AS target_col_name, "
        "        string_agg(n_src.nspname || '.' || c_src.relname || '.' || a_src.attname, ', ') AS referring_columns "
        "    FROM pg_constraint con "
        "    JOIN pg_class c_src ON con.conrelid = c_src.oid "
        "    JOIN pg_namespace n_src ON c_src.relnamespace = n_src.oid "
        "    CROSS JOIN LATERAL unnest(con.conkey, con.confkey) AS refs(src_col_num, target_col_num) "
        "    JOIN pg_attribute a_src ON a_src.attrelid = con.conrelid AND a_src.attnum = refs.src_col_num "
        "    JOIN pg_attribute a_target ON a_target.attrelid = con.confrelid AND a_target.attnum = refs.target_col_num "
        "    WHERE con.contype = 'f' "
        "    GROUP BY con.confrelid, a_target.attname "
        "), "
        "column_constraints AS ( "
        "    SELECT "
        "        a.attrelid, "
        "        a.attname, "
        "        a.attnum, "
        "        BOOL_OR(c.contype = 'p') AS is_pk, "
        "        BOOL_OR(c.contype = 'u') AS is_unique_constraint, "
        "        BOOL_OR(c.contype = 'c') AS is_check_constraint, "
        "        BOOL_OR(EXISTS ( "
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
        "column_stats AS ( "
        "    SELECT  "
        "        n.nspname AS table_schema, "
        "        c.relname AS table_name, "
        "        a.attname AS column_name, "
        "        pg_catalog.format_type(a.atttypid, a.atttypmod) AS data_type, "
        "        a.attnum AS column_number, "
        "        a.attrelid as table_oid, "
    "    (t.typelem != 0 AND t.typlen = -1) AS is_array, "
        "        CASE  "
        "            WHEN a.atttypid IN (1042, 1043, 25) THEN "
        "                CASE  "
        "                    WHEN a.atttypmod = -1 THEN -1 "
        "                    ELSE a.atttypmod - 4 "
        "                END "
        "            ELSE -1 "
        "        END AS max_length, "
        "        (SELECT COUNT(*) FROM pg_constraint  "
        "         WHERE conrelid = c.oid AND contype = 'p' AND a.attnum = ANY(conkey)) AS is_primary_key, "
        "        (SELECT COUNT(*) FROM pg_constraint  "
        "         WHERE conrelid = c.oid AND contype = 'f' AND a.attnum = ANY(conkey)) AS is_foreign_key, "
        "        (SELECT COUNT(*) FROM pg_constraint  "
        "         WHERE conrelid = c.oid AND contype = 'u' AND a.attnum = ANY(conkey)) AS is_unique, "
        "        (SELECT COUNT(*) FROM pg_constraint  "
        "         WHERE conrelid = c.oid AND contype = 'c' AND a.attnum = ANY(conkey)) AS is_check, "
        "        (SELECT COUNT(*) FROM pg_index  "
        "         WHERE indrelid = c.oid AND indisunique = true AND indisprimary = false  "
        "         AND a.attnum = ANY(indkey)) AS is_unique_index "
        "    FROM pg_attribute a "
        "    JOIN pg_class c ON a.attrelid = c.oid "
        "    JOIN pg_namespace n ON c.relnamespace = n.oid "
    "    JOIN pg_type t ON a.atttypid = t.oid "
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
            "    cs.table_schema,"
            "    cs.table_name, "
            "    cs.column_name, "
            "    cs.data_type, "
            "    tc.row_count, "
            "    CASE "
            "        WHEN tc.row_count = 0 THEN 0 "
            "        ELSE ROUND((s.null_frac * 100)::numeric, 2) "
            "    END AS null_percent, "
            "    TRIM( "
            "        CASE WHEN cs.is_primary_key > 0 THEN 'PK ' ELSE '' END || "
            "        CASE WHEN cs.is_foreign_key > 0 THEN 'FK ' ELSE '' END || "
            "        CASE WHEN cs.is_unique > 0 OR cs.is_unique_index > 0 THEN 'UNIQUE ' ELSE '' END || "
            "        CASE WHEN cs.is_check > 0 THEN 'CHECK' ELSE '' END "
            "    ) AS modifiers, "
            "    cui.composite_unique_peers, "
            "    cfk.peers AS composite_fk_peers, "
            "    inc.referring_columns AS incoming_references, "
            "    cs.max_length, "
            "    out_ref.outcoming_references, "
            "    out_ref.relation_types, "
    "        CASE WHEN cs.is_array THEN s.most_common_elems ELSE s.most_common_vals END AS mcv, "
    "    CASE WHEN cs.is_array THEN s.most_common_elem_freqs ELSE s.most_common_freqs END AS mcv_frequencies, " 
            "    s.avg_width AS avg_column_width_bytes, "
            "    s.n_distinct AS ndistinct, "
            "    s.histogram_bounds as hbounds "
            "FROM column_stats cs "
            "JOIN table_counts tc ON cs.table_schema = tc.schemaname AND cs.table_name = tc.table_name "
            "LEFT JOIN aggregated_references out_ref ON out_ref.conrelid = cs.table_oid AND out_ref.src_column = cs.column_name "
            "LEFT JOIN pg_stats s ON s.schemaname = cs.table_schema "
            "                     AND s.tablename = cs.table_name "
            "                     AND s.attname = cs.column_name "
            "LEFT JOIN composite_unique_info cui ON cui.attrelid = cs.table_oid "
            "                                   AND cui.attname = cs.column_name "
            "LEFT JOIN composite_fk_info cfk ON cfk.attrelid = cs.table_oid AND cfk.attname = cs.column_name "
            "LEFT JOIN incoming_references_info inc ON inc.target_table_oid = cs.table_oid AND inc.target_col_name = cs.column_name "
            "ORDER BY "
            "    cs.table_schema, "
            "    cs.table_name, "
            "    cs.column_number "

        );
        resQuery = (char*) MemoryContextAlloc(upper_ctx, statQuery.len + 1);
        memcpy(resQuery, statQuery.data, statQuery.len + 1);
        SPI_finish();

        elog(LOG, "\n%s\n", resQuery);
        
        

        
        funcctx = SRF_FIRSTCALL_INIT();

        if (SPI_connect() != SPI_OK_CONNECT)
        ereport(ERROR, (errmsg("SPI_connect failed")));

        spi_ret = SPI_execute(resQuery, true, 0);  

        if (spi_ret < 0)
            ereport(ERROR,
                    (errmsg("SPI_execute failed with code %d", spi_ret),
                        errdetail("Query: %s", resQuery)));


        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
        
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                            errmsg("function returning record called in context that cannot accept type record")));

        
        tuples = (HeapTuple*)palloc(sizeof(HeapTuple) * SPI_processed);
        for (int i = 0; i < SPI_processed; i++) {
            tuples[i] = heap_copytuple(SPI_tuptable->vals[i]);
            elog(LOG, "\n%x\n", tuples[i]->t_data);
        }
        max_calls = SPI_processed;
           
        spi_tupdesc = SPI_tuptable->tupdesc;
        out_tupdesc = CreateTupleDescCopy(spi_tupdesc);
        
        elog(LOG, "\n%x\n", tuples[0]->t_data);
        funcctx->user_fctx = tuples;
        funcctx->max_calls = max_calls;
        funcctx->tuple_desc = BlessTupleDesc(tupdesc);
        attinmeta = TupleDescGetAttInMetadata(tupdesc);
        funcctx->attinmeta = attinmeta;
        elog(LOG, "\n\nzhopa\n\n");

        
        //
        MemoryContextSwitchTo(oldcontext);
    }

    funcctx = SRF_PERCALL_SETUP();
    attinmeta = funcctx->attinmeta;
    int count = 0;
    elog(LOG, "\n\n call cntr =  %d \n\n ", funcctx->call_cntr);
    if (funcctx->call_cntr < funcctx->max_calls)
    {   
        HeapTuple *tuples = (HeapTuple *) funcctx->user_fctx;
        TupleDesc tupdesc = funcctx->tuple_desc;
        //HeapTuple tuple = tuples[funcctx->call_cntr];
        char* values[18];
        bool  nulls[18];
        
        memset(values, 0, sizeof(values));
        memset(nulls,  true, sizeof(nulls)); 
        
        //HeapTuple current_tuple = SPI_tuptable->vals[funcctx->call_cntr];
        for (int i = 0; i < 18; i++) {
            values[i] = SPI_getvalue(tuples[funcctx->call_cntr], SPI_tuptable->tupdesc, i+1);
            //elog(LOG, "\n %s \n", values[i]);
        }
        

        HeapTuple tuple;
        Datum     result;
        
        tuple = BuildTupleFromCStrings(attinmeta, values);
        
        result = HeapTupleGetDatum(tuple);
        count++;

        SRF_RETURN_NEXT(funcctx, result);
    }
    else
    {
        SPI_finish();
        for (int i = 0; i < count; i++) {
            pfree(tuples[i]);
        }
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
                    "EXPLAIN (FORMAT JSON, VERBOSE, ANALYZE) ");
                } else {
                    appendStringInfoString(&explainQuery,
                    "EXPLAIN (FORMAT JSON, VERBOSE) ");
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
        PG_RETURN_NULL();
    }
    PG_RETURN_NULL();
}