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

PG_MODULE_MAGIC;

typedef struct TableInfo
{
    Oid oid;
    char *name;
    char *schema;
} TableInfo;

void generate_table_ddl(StringInfo buf, Oid tableOid);
TableInfo** get_user_tables(int *table_count);
void generate_tables_ddl(StringInfo buf);
void generate_views_ddl(StringInfo buf);
void generate_indexes_ddl(StringInfo buf);
void generate_extensions_ddl(StringInfo buf);
void generate_sequences_ddl(StringInfo buf);
char* get_sequence_owned_by(const char* nspname, const char* seqname);
PG_FUNCTION_INFO_V1(dump_schema);

Datum
dump_schema(PG_FUNCTION_ARGS)
{
    int spi;
    
    StringInfoData buf;
    
    initStringInfo(&buf);
    
    appendStringInfo(&buf, "--PostgreSQL database schema dump (tables only)\n");
    appendStringInfo(&buf, "SET statement_timeout = 0;\n");
    appendStringInfo(&buf, "SET lock_timeout = 0;\n");
    appendStringInfo(&buf, "SET idle_in_transaction_session_timeout = 0;\n");
    appendStringInfo(&buf, "SET client_encoding = 'UTF8';\n");
    appendStringInfo(&buf, "SET standard_conforming_strings = on;\n");
    appendStringInfo(&buf, "SELECT pg_catalog.set_config('search_path', '', false);\n\n");
    
    if ((spi = SPI_connect()) == SPI_OK_CONNECT){
        generate_extensions_ddl(&buf);

        generate_schemas_ddl(&buf);
    
        generate_sequences_ddl(&buf);
        
        generate_tables_ddl(&buf);

        generate_views_ddl(&buf);
        
        generate_indexes_ddl(&buf);
    }
        
    
    
    elog(LOG, "%s\n\n", buf.data);
    
    text* ret = cstring_to_text(buf.data);
    //SPI_finish();
    PG_RETURN_TEXT_P(ret);
}



void generate_sequences_ddl(StringInfo buf) {
    int ret;
    char *query;
    
    query = "SELECT schemaname, sequencename, "
            "sequenceowner, start_value, min_value, max_value, "
            "increment_by, cycle, cache_size, last_value, "
            "pg_catalog.obj_description(pg_sequence.seqrelid, 'pg_class') as description "
            "FROM pg_sequences "
            "JOIN pg_sequence ON pg_sequence.seqrelid = pg_sequences.sequencename::regclass "
            "WHERE schemaname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "ORDER BY schemaname, sequencename";
    
    ret = SPI_execute(query, true, 0);
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        appendStringInfoString(buf, "--\n-- Sequences\n--\n\n");
        
        for (int i = 0; i < SPI_processed; i++)
        {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull[11];
            
            char* nspname = SPI_getvalue(tuple, tupdesc, 1);
            char* seqname = SPI_getvalue(tuple, tupdesc, 2);
            char* owner = SPI_getvalue(tuple, tupdesc, 3);
            Datum start_value_datum = SPI_getbinval(tuple, tupdesc, 4, &isNull[3]);
            Datum min_value_datum = SPI_getbinval(tuple, tupdesc, 5, &isNull[4]);
            Datum max_value_datum = SPI_getbinval(tuple, tupdesc, 6, &isNull[5]);
            Datum increment_datum = SPI_getbinval(tuple, tupdesc, 7, &isNull[6]);
            Datum cycle_datum = SPI_getbinval(tuple, tupdesc, 8, &isNull[7]);
            Datum cache_datum = SPI_getbinval(tuple, tupdesc, 9, &isNull[8]);
            Datum last_value_datum = SPI_getbinval(tuple, tupdesc, 10, &isNull[9]);
            char* description = SPI_getvalue(tuple, tupdesc, 11);
            
            
            
            
            if (nspname && seqname)
            {
                int64 start_value = !isNull[3] ? DatumGetInt64(start_value_datum) : 1;
                int64 min_value = !isNull[4] ? DatumGetInt64(min_value_datum) : 1;
                int64 max_value = !isNull[5] ? DatumGetInt64(max_value_datum) : 0;
                int64 increment = !isNull[6] ? DatumGetInt64(increment_datum) : 1;
                bool cycle = !isNull[7] ? DatumGetBool(cycle_datum) : false;
                int64 cache = !isNull[8] ? DatumGetInt64(cache_datum) : 1;
                int64 last_value = !isNull[9] ? DatumGetInt64(last_value_datum) : 0;

                elog(LOG, "\n\nINCREMENT = %d\n\n", increment);
                appendStringInfo(buf, "CREATE SEQUENCE %s.%s", nspname, seqname);
                
                appendStringInfo(buf, "\n    INCREMENT BY %ld", increment);
                
                appendStringInfo(buf, "\n    MINVALUE %ld", min_value);
                
                if (max_value != 0) 
                {
                    appendStringInfo(buf, "\n    MAXVALUE %ld", max_value);
                }
                else
                {
                    appendStringInfoString(buf, "\n    NO MAXVALUE");
                }
                
                if (start_value != 1)
                {
                    appendStringInfo(buf, "\n    START WITH %ld", start_value);
                }
                
                
                if (cache != 1)
                {
                    appendStringInfo(buf, "\n    CACHE %ld", cache);
                }
                
                
                if (cycle)
                {
                    appendStringInfoString(buf, "\n    CYCLE");
                }
                else
                {
                    appendStringInfoString(buf, "\n    NO CYCLE");
                }
                
                appendStringInfoString(buf, ";");
                
                char *owned_by = get_sequence_owned_by(nspname, seqname);
                if (owned_by)
                {
                    appendStringInfo(buf, "\nALTER SEQUENCE %s.%s OWNED BY %s;", 
                                   nspname, seqname, owned_by);
                    pfree(owned_by);
                }
                
                if (last_value > 0)
                {
                    appendStringInfo(buf, "\nSELECT pg_catalog.setval('%s.%s', %ld, false);", 
                                   nspname, seqname, last_value);
                }
                
                appendStringInfoString(buf, "\n\n");
                
                if (description)
                {
                    appendStringInfo(buf, "COMMENT ON SEQUENCE %s.%s IS '%s';\n\n", 
                                   nspname, seqname, description);
                }
                
                pfree(nspname);
                pfree(seqname);
                elog(LOG, "OWNER = %s\n\n", owner);
                if (owner) pfree(owner);
                if (description) pfree(description);
             }
        }
    }
}
char *
get_sequence_owned_by(const char *nspname, const char *seqname)
{
    int ret;
    char *query;
    char *owned_by = NULL;
    
    query = "SELECT n.nspname, c.relname, a.attname "
            "FROM pg_depend d "
            "JOIN pg_class s ON s.oid = d.objid "
            "JOIN pg_class c ON c.oid = d.refobjid "
            "JOIN pg_attribute a ON a.attrelid = d.refobjid AND a.attnum = d.refobjsubid "
            "JOIN pg_namespace n ON n.oid = c.relnamespace "
            "WHERE s.relname = $1 "
            "AND n.nspname = $2 "
            "AND d.deptype = 'a' "  
            "AND s.relkind = 'S'";
    
    ret = SPI_execute_with_args(query, 2, 
                               (Oid[]) {TEXTOID, TEXTOID},
                               (Datum[]) {CStringGetTextDatum(seqname), CStringGetTextDatum(nspname)},
                               (bool[]) {false, false},
                               true, 0);
    
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        HeapTuple tuple = SPI_tuptable->vals[0];
        bool isNull1, isNull2, isNull3;
        
        char* table_nspname = SPI_getvalue(tuple, tupdesc, 1);
        char* table_name = SPI_getvalue(tuple, tupdesc, 2);
        char* column_name = SPI_getvalue(tuple, tupdesc, 3);
        
        if (table_nspname && table_name && column_name)
        {
            owned_by = psprintf("%s.%s.%s", table_nspname, table_name, column_name);
            
            pfree(table_nspname);
            pfree(table_name);
            pfree(column_name);
        }
    }
    
    return owned_by;
}

void generate_extensions_ddl(StringInfo buf) {
    int ret;
    char *query;
    
    query = "SELECT e.extname, e.extversion, n.nspname, "
            "CASE WHEN e.extrelocatable THEN 'true' ELSE 'false' END as extrelocatable, "
            "pg_catalog.obj_description(e.oid, 'pg_extension') as description "
            "FROM pg_extension e "
            "LEFT JOIN pg_namespace n ON n.oid = e.extnamespace "
            "ORDER BY e.extname";
    
    ret = SPI_execute(query, true, 0);
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        appendStringInfoString(buf, "--\n-- Extensions\n--\n\n");
        
        for (int i = 0; i < SPI_processed; i++)
        {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull1, isNull2, isNull3, isNull4, isNull5;
            
            char* extname = SPI_getvalue(tuple, tupdesc, 1);
            char* extversion = SPI_getvalue(tuple, tupdesc, 2);
            char* nspname = SPI_getvalue(tuple, tupdesc, 3);
            char* extrelocatable = SPI_getvalue(tuple, tupdesc, 4);
            char* description = SPI_getvalue(tuple, tupdesc, 5);
            
            if (extname && extversion)
            {
                
                appendStringInfo(buf, "CREATE EXTENSION IF NOT EXISTS %s", extname);
                
                
                if (nspname && strcmp(nspname, "public") != 0)
                {
                    appendStringInfo(buf, " WITH SCHEMA %s", nspname);
                }
                
                
                appendStringInfo(buf, " VERSION '%s'", extversion);
                
                
                appendStringInfoString(buf, ";\n");
                
                
                if (description)
                {
                    appendStringInfo(buf, "COMMENT ON EXTENSION %s IS '%s';\n", 
                                   extname, description);
                }
                
                appendStringInfoString(buf, "\n");
                
                pfree(extname);
                pfree(extversion);
                if (nspname) pfree(nspname);
                if (extrelocatable) pfree(extrelocatable);
                if (description) pfree(description);
            }
        }
    }
}

void generate_tables_ddl(StringInfo buf) {
    int ret;
    char *query;
    
    query = "SELECT c.oid "
            "FROM pg_class c "
            "JOIN pg_namespace n ON c.relnamespace = n.oid "
            "WHERE c.relkind = 'r' "
            "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "ORDER BY n.nspname, c.relname";
    
    ret = SPI_execute(query, true, 0);
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        appendStringInfoString(buf, "--\n-- Tables\n--\n\n");

        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < SPI_processed; i++)
        {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull;
            
            Datum oid_datum = SPI_getbinval(tuple, tupdesc, 1, &isNull);
            if (!isNull)
            {
                Oid tableOid = DatumGetObjectId(oid_datum);
                generate_table_ddl(buf, tableOid);
                appendStringInfoString(buf, "\n\n");
            }
        }
    }
}
 
void generate_views_ddl(StringInfo buf) {
    int ret;
    char *query;
    
    query = "SELECT c.oid, n.nspname, c.relname, "
            "pg_catalog.pg_get_viewdef(c.oid, true) as definition "
            "FROM pg_class c "
            "JOIN pg_namespace n ON c.relnamespace = n.oid "
            "WHERE c.relkind = 'v' "
            "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "ORDER BY n.nspname, c.relname";
    
    ret = SPI_execute(query, true, 0);
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        appendStringInfoString(buf, "--\n-- Views\n--\n\n");
        
        for (int i = 0; i < SPI_processed; i++)
        {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull1, isNull2, isNull3, isNull4;
            
            char* nspname = SPI_getvalue(tuple, tupdesc, 2);
            char* relname = SPI_getvalue(tuple, tupdesc, 3);
            char* definition = SPI_getvalue(tuple, tupdesc, 4);
            
            if (nspname && relname && definition)
            {
                
                
                appendStringInfo(buf, "CREATE VIEW %s.%s AS\n%s;\n\n", 
                               nspname, relname, definition);
                
                Oid viewOid;
                Datum oid_datum = SPI_getbinval(tuple, tupdesc, 1, &isNull4);
                if (!isNull4)
                {
                    viewOid = DatumGetObjectId(oid_datum);
                    char *view_comment = GetComment(viewOid, RelationRelationId, 0);
                    if (view_comment)
                    {
                        appendStringInfo(buf, "COMMENT ON VIEW %s.%s IS '%s';\n\n", 
                                       nspname, relname, view_comment);
                        pfree(view_comment);
                    }
                }
                
                pfree(nspname);
                pfree(relname);
                pfree(definition);
            }
        }
    }
}

void generate_indexes_ddl(StringInfo buf) {
int ret;
    char *query;
    
    query = "SELECT n.nspname, c.relname as tablename, "
            "i.relname as indexname, "
            "pg_catalog.pg_get_indexdef(i.oid) as indexdef, "
            "i.oid as index_oid "
            "FROM pg_index x "
            "JOIN pg_class i ON i.oid = x.indexrelid "
            "JOIN pg_class c ON c.oid = x.indrelid "
            "JOIN pg_namespace n ON n.oid = i.relnamespace "
            "WHERE i.relkind = 'i' "
            "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "AND NOT x.indisprimary "  
            "ORDER BY n.nspname, c.relname, i.relname";
    
    ret = SPI_execute(query, true, 0);
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        appendStringInfoString(buf, "--\n-- Indexes\n--\n\n");
        
        for (int i = 0; i < SPI_processed; i++)
        {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull1, isNull2, isNull3, isNull4;
            
            char* nspname = SPI_getvalue(tuple, tupdesc, 1);
            char* tablename = SPI_getvalue(tuple, tupdesc, 2);
            char* indexname = SPI_getvalue(tuple, tupdesc, 3);
            char* indexdef = SPI_getvalue(tuple, tupdesc, 4);
     
            if (nspname && tablename && indexname)
            {
                
                
                appendStringInfoString(buf, indexdef);
                appendStringInfoString(buf, ";\n\n");
                
                Oid indexOid;
                Datum oid_datum = SPI_getbinval(tuple, tupdesc, 5, &isNull4);
                if (indexdef)
                {
                    indexOid = DatumGetObjectId(oid_datum);
                    char *index_comment = GetComment(indexOid, RelationRelationId, 0);
                    if (index_comment)
                    {
                        appendStringInfo(buf, "COMMENT ON INDEX %s IS '%s';\n\n", 
                                       indexname, index_comment);
                        pfree(index_comment);
                    }
                }
                
                pfree(nspname);
                pfree(tablename);
                pfree(indexname);
                pfree(indexdef);
            }
        }
    }
}

void
generate_schemas_ddl(StringInfo buf) {
    int ret;
    char *query;

    query = "SELECT n.oid, n.nspname, pg_catalog.pg_get_userbyid(n.nspowner) as owner, "
            "pg_catalog.obj_description(n.oid, 'pg_namespace') as description "
            "FROM pg_catalog.pg_namespace n "
            "WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' "
            "ORDER BY n.nspname";
    
    ret = SPI_execute(query, true, 0);
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        appendStringInfoString(buf, "--\n-- Schemas\n--\n\n");

        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < SPI_processed; i++)
        {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull1, isNull2, isNull3, isNull4;
            
            char* nspname = SPI_getvalue(tuple, tupdesc, 2);
            char* owner = SPI_getvalue(tuple, tupdesc, 3);
            char* description = SPI_getvalue(tuple, tupdesc, 4);
            
            if (nspname)
            {
                
                appendStringInfo(buf, "CREATE SCHEMA %s", nspname);
                
                if (owner && strcmp(owner, GetUserNameFromId(GetUserId(), false)) != 0)
                {
                    appendStringInfo(buf, " AUTHORIZATION %s", owner);
                }
                
                appendStringInfoString(buf, ";\n");
                
               
                if (description)
                {
                    appendStringInfo(buf, "COMMENT ON SCHEMA %s IS '%s';\n", 
                                   nspname, description);
                }
                
                appendStringInfoString(buf, "\n");
                
                pfree(nspname);
                if (owner) pfree(owner);
                if (description) pfree(description);
            }
        }
    }
}


 void
generate_table_ddl(StringInfo buf, Oid tableOid)
{
    Relation rel;
    TupleDesc tupdesc;
    char *relname;
    char *nspname;
    int i;
    bool first_col = true;
    
    if (!OidIsValid(tableOid))
        return;
    
    rel = table_open(tableOid, AccessShareLock);
    
    tupdesc = RelationGetDescr(rel);
    nspname = get_namespace_name(RelationGetNamespace(rel));
    if (!nspname)
        nspname = pstrdup("public");
    
    relname = pstrdup(NameStr(rel->rd_rel->relname));
    
    appendStringInfo(buf, "CREATE TABLE %s.%s (\n", nspname, relname);
    
    for (i = 0; i < tupdesc->natts; i++)
    {
        Form_pg_attribute attr;
        
        attr = TupleDescAttr(tupdesc, i);

        
        if (attr->attisdropped)
            continue;
        
        if (!first_col)
            appendStringInfoString(buf, ",\n");
        first_col = false;
        
        appendStringInfo(buf, "    %s ", NameStr(attr->attname));
        
        char *type_name = format_type_be(attr->atttypid);
        appendStringInfoString(buf, type_name);
        
        if (attr->atttypmod != -1)
        {
            if (attr->atttypid == VARCHAROID || attr->atttypid == BPCHAROID)
            {
                appendStringInfo(buf, "(%d)", attr->atttypmod - VARHDRSZ);
            }
            else if (attr->atttypid == NUMERICOID)
            {
                int32 precision = (attr->atttypmod >> 16) & 0xFFFF;
                int32 scale = attr->atttypmod & 0xFFFF;
                if (scale > 0)
                    appendStringInfo(buf, "(%d,%d)", precision - 4, scale);
                else
                    appendStringInfo(buf, "(%d)", precision - 4);
            }
        }
        
        if (attr->attnotnull)
            appendStringInfoString(buf, " NOT NULL");
        
    }
    
    appendStringInfoString(buf, "\n);\n");
    
    char *table_comment = GetComment(tableOid, RelationRelationId, 0);
    if (table_comment)
    {
        appendStringInfo(buf, "COMMENT ON TABLE %s.%s IS '%s';\n", 
                       nspname, relname, table_comment);
        pfree(table_comment);
    }
    
    for (i = 0; i < tupdesc->natts; i++)
    {
        Form_pg_attribute attr;

        attr = TupleDescAttr(tupdesc, i);

        
        if (attr->attisdropped)
            continue;
        
        char *col_comment = GetComment(tableOid, RelationRelationId, attr->attnum);
        if (col_comment)
        {
            appendStringInfo(buf, "COMMENT ON COLUMN %s.%s.%s IS '%s';\n", 
                           nspname, relname, NameStr(attr->attname), col_comment);
            pfree(col_comment);
        }
    }
    

    table_close(rel, AccessShareLock);
    
    pfree(relname);
    pfree(nspname);
}


