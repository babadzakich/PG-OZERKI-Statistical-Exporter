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

void cleanup_tables(TableInfo **tables, int count);

char* get_schema_name(Oid namespaceOid);
PG_FUNCTION_INFO_V1(dump_schema);

Datum
dump_schema(PG_FUNCTION_ARGS)
{
    int table_count = 0;
    TableInfo **tables = get_user_tables(&table_count);
    StringInfoData buf;
    
    initStringInfo(&buf);
    
    appendStringInfo(&buf, "--PostgreSQL database schema dump (tables only)\n");
    appendStringInfo(&buf, "SET statement_timeout = 0;\n");
    appendStringInfo(&buf, "SET lock_timeout = 0;\n");
    appendStringInfo(&buf, "SET idle_in_transaction_session_timeout = 0;\n");
    appendStringInfo(&buf, "SET client_encoding = 'UTF8';\n");
    appendStringInfo(&buf, "SET standard_conforming_strings = on;\n");
    appendStringInfo(&buf, "SELECT pg_catalog.set_config('search_path', '', false);\n\n");
    
    elog(LOG, "GOT USER TABLES\n\n");
    for (int i = 0; i < table_count; i++)
    {
        TableInfo *table = tables[i];
        //elog(LOG, "OID = %s\n\n", table->name);
        generate_table_ddl(&buf, table->oid);
        appendStringInfo(&buf, "\n\n");
    }
    elog(LOG, "END LOOP\n\n");
    cleanup_tables(tables, table_count);
    elog(LOG, "CLEANED UP\n\n");
    elog(LOG, "%s\n\n", buf.data);
    
    text* ret = cstring_to_text(buf.data);
    //SPI_finish();
    PG_RETURN_TEXT_P(ret);
}

 TableInfo**
get_user_tables(int *table_count)
{
    TableInfo **tables = NULL;
    int ret;
    int count = 0;
    
    *table_count = 0;
    
    ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
    {
        elog(WARNING, "SPI_connect failed");
        return NULL;
    }
    
    char *query = "SELECT c.oid, c.relname, n.nspname "
                  "FROM pg_class c "
                  "JOIN pg_namespace n ON c.relnamespace = n.oid "
                  "WHERE c.relkind = 'r' "
                  "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
                  "ORDER BY n.nspname, c.relname";
    
    ret = SPI_execute(query, true, 0);
    if (ret != SPI_OK_SELECT)
    {
        elog(WARNING, "SPI_execute failed: %d", ret);
        SPI_finish();
        return NULL;
    }
    
    if (SPI_processed > 0)
    {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        SPITupleTable *tuptable = SPI_tuptable;
        
        tables = (TableInfo **) palloc0(SPI_processed * sizeof(TableInfo *));
        if (tables == NULL) {
            elog(DEBUG1, "PALLOC FAILED\n");
        } else {
            elog(DEBUG1, "PALLOC SUCCESS\n");
        }
        for (int i = 0; i < SPI_processed; i++)
        {
             elog(DEBUG1, "count = %d, spi proceed = %d", 
                  count, SPI_processed);
            HeapTuple tuple = tuptable->vals[i];
            TableInfo *table;
            Datum oid_datum, relname_datum, nspname_datum;
            bool isNull;
            
            table = (TableInfo *) palloc0(sizeof(TableInfo));
            if (table == NULL) {
                elog(DEBUG1, "PALLOC FAILED\n");
            } else {
                elog(DEBUG1, "PALLOC SUCCESS\n");
            }
            
            oid_datum = SPI_getbinval(tuple, tupdesc, 1, &isNull);
            if (isNull)
            {
                pfree(table);
                continue;
            }
            table->oid = DatumGetObjectId(oid_datum);
            
            char *relname_cstr = SPI_getvalue(tuple, tupdesc, 2);
            if (!relname_cstr)
            {
                pfree(table);
                continue;
            }
            table->name = pstrdup(relname_cstr);
            pfree(relname_cstr);
            
            char *nspname_cstr = SPI_getvalue(tuple, tupdesc, 3);
            if (!nspname_cstr)
            {
                pfree(table->name);
                pfree(table);
                continue;
            }
            table->schema = pstrdup(nspname_cstr);
            pfree(nspname_cstr);
            elog(DEBUG1, "ABOBA\n\n");
            tables[count] = table;
            count++;
            
            elog(DEBUG1, "Added table: %s.%s (OID: %u)", 
                 table->schema, table->name, table->oid);
        }
    }
    
    //SPI_finish();
    *table_count = count;
    
    return tables;
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
    
    appendStringInfoString(buf, "\n);");
    

    table_close(rel, AccessShareLock);

    
    pfree(relname);
    pfree(nspname);
}

char *
get_schema_name(Oid namespaceOid)
{
    HeapTuple tuple;
    char *nspname;
    
    tuple = SearchSysCache1(NAMESPACEOID, ObjectIdGetDatum(namespaceOid));
    if (!HeapTupleIsValid(tuple))
        return NULL;
    
    Form_pg_namespace nspForm = (Form_pg_namespace) GETSTRUCT(tuple);
    nspname = pstrdup(NameStr(nspForm->nspname));
    
    ReleaseSysCache(tuple);
    return nspname;
}

 void
cleanup_tables(TableInfo **tables, int count)
{
     if (!tables)
        return;
        
    for (int i = 0; i < count; i++)
    {
        TableInfo *table = tables[i];
        if (table)
        {
            if (table->name)
                pfree(table->name);
            if (table->schema)
                pfree(table->schema);
            pfree(table);
        }
    }
    pfree(tables);
}