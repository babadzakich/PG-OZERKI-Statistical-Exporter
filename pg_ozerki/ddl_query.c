
#include "ozerki_utils.h"
#include "ddl_query.h"
#include "catalog/namespace.h"
#include "parser/analyze.h"
#include "parser/parser.h"
#include "utils/lsyscache.h" 
#include "catalog/pg_class.h"
#include "tcop/tcopprot.h"
void add_schema_to_deps(QueryDependencies *deps, const char *schema);

void
find_sequences_for_tables(QueryDependencies *deps);

void find_views_for_tables(QueryDependencies *deps, char* raw_query);


QueryDependencies* init_deps() {
    QueryDependencies *deps = (QueryDependencies*)palloc0(sizeof(QueryDependencies));
    

    deps->tableNames = NULL;
    deps->viewOids = NULL;
    deps->functionOids = NULL;
    deps->sequenceOids = NULL;
    deps->indexOids = NULL;
    deps->constraintOids = NULL;
    deps->schemas = NULL;
    
    deps->tableCount = 0;
    deps->viewCount = 0;
    deps->functionCount = 0;
    deps->sequenceCount = 0;
    deps->indexCount = 0;
    deps->constraintCount = 0;
    deps->schemaCount = 0;

    deps->been_analyzed = false;
    return deps;
}

static void
add_table(QueryDependencies *deps, const char *name)
{
    if (deps->tableCount == 0) {
        deps->tableNames = palloc(sizeof(char *));
    } else {
        deps->tableNames = repalloc(
            deps->tableNames,
            sizeof(char *) * (deps->tableCount + 1)
        );
    }
    deps->tableNames[deps->tableCount] = pstrdup(name);
    deps->tableCount++;
}

void add_view_to_deps(QueryDependencies *deps, Oid viewOid) {
    for (int i = 0; i < deps->viewCount; i++) {
        if (deps->viewOids[i] == viewOid) return;
    }
    
    if (deps->viewCount == 0)
        deps->viewOids = palloc(sizeof(Oid));
    else
        deps->viewOids = repalloc(deps->viewOids, sizeof(Oid) * (deps->viewCount + 1));
        
    deps->viewOids[deps->viewCount++] = viewOid;
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
    
    elog(LOG, "\n\nDDL CALLED\n\n");
    if (!OidIsValid(tableOid)) {
        return;
    }
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
                elog(LOG, "\n\nATTYPMOD = %x, PRECISION = %d, SCALE = %d\n\n", attr->atttypmod, precision, scale );
                
                if (scale - 4 > 0) {
                    if (scale - 4 > 1000) {
                        scale -= 2048;
                    }
                    appendStringInfo(buf, "(%d,%d)", precision, scale - 4);
                }
                else
                    appendStringInfo(buf, "(%d)", precision);
            }
        }
        
         

        if (attr->attnotnull)
            appendStringInfoString(buf, " NOT NULL");
        
    }
    
    char *pk_constraint = get_primary_key_constraint(tableOid);
    if (pk_constraint)
    {
        appendStringInfo(buf, ",\n    %s", pk_constraint);
        pfree(pk_constraint);
    }
    
    char *table_check_constraints = get_table_check_constraints(tableOid);
    if (table_check_constraints)
    {
        appendStringInfoString(buf, table_check_constraints);
        pfree(table_check_constraints);
    }
    
    appendStringInfoString(buf, "\n);\n\n");


    
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

static void
extract_tables_from_yaml(const char *plan, QueryDependencies* deps)
{
    
    const char *p = plan;

    //deps = palloc0(sizeof(QueryDependencies));

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
    }


}

void add_table_constraints_to_deps(QueryDependencies *deps) {
    if (!deps || deps->tableCount == 0) {
        elog(LOG, "\n\nHUI  PIZDA\n\n");
        return;
    }
    

    StringInfoData constrQuery;
    initStringInfo(&constrQuery);
    
    appendStringInfo(&constrQuery,
        "SELECT c.oid, c.contype, c.confrelid "
        "FROM pg_constraint c "
        "JOIN pg_class t ON t.oid = c.conrelid "
        "WHERE t.oid IN (");
    Oid* tableOids = palloc(deps->tableCount * sizeof(Oid));
    for (int i = 0; i < deps->tableCount; i++) {
        if (i > 0) appendStringInfoString(&constrQuery, ", ");
        tableOids[i] = table_name_to_oid_internal(deps->tableNames[i]);
        appendStringInfo(&constrQuery, "%u", tableOids[i]);
        
    }
    
    appendStringInfo(&constrQuery, ") AND c.contype IN ('p', 'f', 'c', 'u')");
    
    elog(LOG, "\n\n CONstr query = %s\n\n", constrQuery.data);
    int ret = SPI_execute(constrQuery.data, true, 0);
    
    if (ret == SPI_OK_SELECT && SPI_processed > 0) {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < SPI_processed; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull1, isNull2, isNull3;
            
            Datum constrOid_datum = SPI_getbinval(tuple, tupdesc, 1, &isNull1);
            char *contype = SPI_getvalue(tuple, tupdesc, 2);
            Datum confrelid_datum = SPI_getbinval(tuple, tupdesc, 3, &isNull3);
            
            if (!isNull1 && contype) {
                Oid constrOid = DatumGetObjectId(constrOid_datum);
                

                if (contype[0] == 'f' && !isNull3) {
                    Oid referencedTableOid = DatumGetObjectId(confrelid_datum);
                    

                    bool referenced_table_in_query = false;
                    for (int j = 0; j < deps->tableCount; j++) {
                        if (tableOids[j] == referencedTableOid) {
                            referenced_table_in_query = true;
                            break;
                        }
                    }
                    

                    if (!referenced_table_in_query) {
                        elog(LOG, "Skipping FK constraint OID %u - references table OID %u not in query", 
                             constrOid, referencedTableOid);
                        if (contype) pfree(contype);
                        continue;
                    }
                }
                

                bool exists = false;
                for (int j = 0; j < deps->constraintCount; j++) {
                    if (deps->constraintOids[j] == constrOid) {
                        exists = true;
                        break;
                    }
                }
                
                if (!exists) {
                    if (deps->constraintCount == 0) {
                        deps->constraintOids = (Oid*)palloc(sizeof(Oid));
                    } else {
                        deps->constraintOids = (Oid*)repalloc(deps->constraintOids, 
                                                             (deps->constraintCount + 1) * sizeof(Oid));
                    }
                    deps->constraintOids[deps->constraintCount++] = constrOid;
                    
                    elog(LOG, "Added constraint OID %u (type: %s) for table", 
                         constrOid, contype ? contype : "unknown");
                }
            }
            
            if (contype && !isNull2) pfree(contype);
        }
    }
    
    pfree(constrQuery.data);
}

void add_table_indexes_to_deps(QueryDependencies *deps) {
    if (!deps || deps->tableCount == 0) {
        return;
    }
    
    StringInfoData idxQuery;
    initStringInfo(&idxQuery);
    
    appendStringInfo(&idxQuery,
        "SELECT DISTINCT i.oid "
        "FROM pg_index x "
        "JOIN pg_class i ON i.oid = x.indexrelid "
        "JOIN pg_class t ON t.oid = x.indrelid "
        "WHERE i.relkind = 'i' "
        "AND NOT x.indisprimary "  
        "AND t.oid IN (");
    
    for (int i = 0; i < deps->tableCount; i++) {
        if (i > 0) appendStringInfoString(&idxQuery, ", ");
        appendStringInfo(&idxQuery, "%u", table_name_to_oid_internal(deps->tableNames[i]));
    }
    
    appendStringInfo(&idxQuery, ")");
    
    int ret = SPI_execute(idxQuery.data, true, 0);
    
    if (ret == SPI_OK_SELECT && SPI_processed > 0) {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < SPI_processed; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull;
            
            Datum idxOid_datum = SPI_getbinval(tuple, tupdesc, 1, &isNull);
            
            if (!isNull) {
                Oid idxOid = DatumGetObjectId(idxOid_datum);
                
                bool exists = false;
                for (int j = 0; j < deps->indexCount; j++) {
                    if (deps->indexOids[j] == idxOid) {
                        exists = true;
                        break;
                    }
                }
                
                if (!exists) {
                    if (deps->indexCount == 0) {
                        deps->indexOids = (Oid*)palloc(sizeof(Oid));
                    } else {
                        deps->indexOids = (Oid*)repalloc(deps->indexOids, 
                                                        (deps->indexCount + 1) * sizeof(Oid));
                    }
                    deps->indexOids[deps->indexCount++] = idxOid;
                    
                    elog(LOG, "Added index OID %u for table", idxOid);
                }
            }
        }
    }
    
    pfree(idxQuery.data);
}


void extract_tables_from_query_text(const char *query, QueryDependencies* deps) {
    

    deps->tableNames = NULL;
    deps->viewOids = NULL;
    deps->functionOids = NULL;
    deps->sequenceOids = NULL;
    deps->indexOids = NULL;
    deps->constraintOids = NULL;
    deps->schemas = NULL;
    
    deps->tableCount = 0;
    deps->viewCount = 0;
    deps->functionCount = 0;
    deps->sequenceCount = 0;
    deps->indexCount = 0;
    deps->constraintCount = 0;
    deps->schemaCount = 0;
    
    elog(LOG, "Starting query analysis: %s", query);
    StringInfoData explainQuery;
    initStringInfo(&explainQuery);
    
    appendStringInfoString(&explainQuery,
        "EXPLAIN (FORMAT YAML, VERBOSE) ");
    appendStringInfo(&explainQuery, query);
        
    char* explainQueryCopy = explainQuery.data;
    
    int ret = SPI_execute(explainQueryCopy, false, 0);
    
    if (ret == SPI_OK_UTILITY && SPI_processed > 0) {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        HeapTuple tuple = SPI_tuptable->vals[0];
        char* plan = SPI_getvalue(tuple, tupdesc, 1);
        elog(LOG, "\n\n EXPLAIN RESULT = %s", plan);
        extract_tables_from_yaml(plan, deps);
    }
    elog(LOG, "\n\n EXPLAIN EXECUTED; ret = %d, SPI_PROCESSED = %d", ret, SPI_processed);
    elog(LOG, "Finished analysis. Found %d tables, %d schemas", 
         deps->tableCount, deps->schemaCount);
    
    for (int i = 0; i < deps->tableCount; i++) {
        elog(LOG, "\n table: %s, oid = %d\n", deps->tableNames[i], table_name_to_oid_internal(deps->tableNames[i]));
    }
    
    pfree(explainQuery.data);
    return deps;
}

void analyze_query_dependencies(const char *query, QueryDependencies* deps) {
    
    int ret; 
    
    extract_tables_from_query_text(query, deps);
    find_sequences_for_tables(deps);
    find_views_for_tables(deps, query);
    add_table_constraints_to_deps(deps);
    add_table_indexes_to_deps(deps);
    deps->been_analyzed = true;
  
}

void find_views_for_tables(QueryDependencies *deps, char* query_text)
{
    if (!deps || deps->tableCount == 0){
        return;
    }
    StringInfoData sql;
    initStringInfo(&sql);

    List* raw_parsetree_list;
    List* query_tree_list;
    ListCell *lc;

    raw_parsetree_list = pg_parse_query(query_text);
    
    foreach(lc, raw_parsetree_list) {
        
        RawStmt *raw = (RawStmt *) lfirst(lc);
       
        Query *query;

        
        query = parse_analyze_fixedparams(raw, query_text, NULL, 0,
									  NULL);        
        ListCell *rt_lc;
        foreach(rt_lc, query->rtable) {
            elog(LOG, "\n\n HUI MANDA \n\n");
            RangeTblEntry *rte = (RangeTblEntry *) lfirst(rt_lc);
            
            if (rte->rtekind == RTE_RELATION) {
                Oid relid = rte->relid;
                char relkind = get_rel_relkind(relid);

                if (relkind == RELKIND_VIEW) {
                    elog(LOG, "\n\n FOUND VIEW \n\n");
                    add_view_to_deps(deps, relid);
                } 
            }
        }
    }
    
}


void generate_constraints_ddl_query(StringInfo buf, QueryDependencies *deps) {
    if (deps->constraintCount == 0 && deps->been_analyzed) {
        return;
    }
    int ret;
    char *query;
    elog(LOG, "\n\nCONSTR COUNT = %d\n\n", deps->constraintCount);

    StringInfoData constrQuery;
    initStringInfo(&constrQuery);
    
    appendStringInfo(&constrQuery,
        "SELECT n.nspname, t.relname as tablename, "
        "c.conname, c.contype, "
        "pg_catalog.pg_get_constraintdef(c.oid) as condef, "
        "c.convalidated, c.conislocal, c.coninhcount, "
        "c.oid as conoid "
        "FROM pg_constraint c "
        "JOIN pg_class t ON t.oid = c.conrelid "
        "JOIN pg_namespace n ON n.oid = t.relnamespace "
        "WHERE ");
    if (deps->constraintCount > 0) {
        appendStringInfo(&constrQuery, "c.oid IN (");

        for (int i = 0; i < deps->constraintCount; i++) {
            if (i > 0) appendStringInfoString(&constrQuery, ", ");
            appendStringInfo(&constrQuery, "%u", deps->constraintOids[i]);
        }

        appendStringInfo(&constrQuery, ") AND ");
    }
    appendStringInfo(&constrQuery,
        " n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
        "AND c.contype IN ('f', 'c', 'u') "
        "ORDER BY n.nspname, t.relname, c.contype, c.conname");
    
    query = constrQuery.data;
    
    
    
    ret = SPI_execute(query, true, 0);
    if (ret == SPI_OK_SELECT && SPI_processed > 0) {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        appendStringInfoString(buf, "--\n-- Constraints\n--\n\n");
        
        for (int i = 0; i < SPI_processed; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull[9];
            
            char* nspname = SPI_getvalue(tuple, tupdesc, 1);
            char* tablename = SPI_getvalue(tuple, tupdesc, 2);
            char* conname = SPI_getvalue(tuple, tupdesc, 3);
            char* contype = SPI_getvalue(tuple, tupdesc, 4);
            char* condef = SPI_getvalue(tuple, tupdesc, 5);
            Datum convalidated_datum = SPI_getbinval(tuple, tupdesc, 6, &isNull[5]);
            Datum conislocal_datum = SPI_getbinval(tuple, tupdesc, 7, &isNull[6]);
            Datum coninhcount_datum = SPI_getbinval(tuple, tupdesc, 8, &isNull[7]);
            Datum conoid_datum = SPI_getbinval(tuple, tupdesc, 9, &isNull[8]);
            
            if (nspname && tablename && conname && contype && condef) {
                bool convalidated = !isNull[5] ? DatumGetBool(convalidated_datum) : true;
                bool conislocal = !isNull[6] ? DatumGetBool(conislocal_datum) : true;
                int32 coninhcount = !isNull[7] ? DatumGetInt32(coninhcount_datum) : 0;
                
                if (coninhcount > 0) {

                    if (nspname) pfree(nspname);
                    if (tablename) pfree(tablename);
                    if (conname) pfree(conname);
                    if (condef) pfree(condef);
                    continue;
                }
                
                if (!convalidated) {
                    appendStringInfo(buf, "-- Constraint %s.%s is NOT VALID and needs validation\n", 
                                   tablename, conname);
                    if (nspname) pfree(nspname);
                    if (tablename) pfree(tablename);
                    if (conname) pfree(conname);
                    if (condef) pfree(condef);
                    continue;
                }
                
                char *constraint_type;
                switch (contype[0]) {
                    case 'f':
                        constraint_type = "FOREIGN KEY";
                        break;
                    case 'c':
                        constraint_type = "CHECK";
                        break;
                    case 'u':
                        constraint_type = "UNIQUE";
                        break;
                    default:
                        constraint_type = "CONSTRAINT";
                        break;
                }
                
                appendStringInfo(buf, "ALTER TABLE ONLY %s.%s\n    ADD CONSTRAINT %s ", 
                               nspname, tablename, conname);

                char* schema_added = add_schema_to_constraint(condef, nspname);
                
                if (schema_added) {
                    appendStringInfoString(buf, schema_added);
                    pfree(schema_added);
                }

                if (!convalidated) {
                    appendStringInfoString(buf, " NOT VALID");
                }
                
                appendStringInfoString(buf, ";\n");
                
                Oid conoid = DatumGetObjectId(conoid_datum);
                char *constraint_comment = GetComment(conoid, ConstraintRelationId, 0);
                if (constraint_comment) {
                    appendStringInfo(buf, "COMMENT ON CONSTRAINT %s ON %s.%s IS '%s';\n", 
                                   conname, nspname, tablename, constraint_comment);
                    pfree(constraint_comment);
                }
                
                appendStringInfoString(buf, "\n");
            }
            

            if (nspname) pfree(nspname);
            if (tablename) pfree(tablename);
            if (conname) pfree(conname);
            if (condef) pfree(condef);
        }
    }
    

    if ((deps && deps->constraintCount > 0) || (deps && deps->tableCount > 0)) {
        pfree(query);
    }
}


void generate_sequences_ddl_query(StringInfo buf, QueryDependencies* deps) {
    
    if (deps->sequenceCount == 0 && deps->been_analyzed) {
        return;
    }
    StringInfoData seqQuery;
    initStringInfo(&seqQuery);

    appendStringInfo(&seqQuery,
        "SELECT n.nspname AS schemaname, "
        "c.relname AS sequencename, "
        "r.rolname AS sequenceowner, "
        "s.seqstart AS start_value, "
        "s.seqmin AS min_value, "
        "s.seqmax AS max_value, "
        "s.seqincrement AS increment_by, "
        "s.seqcycle AS cycle, "
        "s.seqcache AS cache_size, "
        "pg_catalog.obj_description(c.oid, 'pg_class') AS description "
        "FROM pg_class c "
        "JOIN pg_namespace n ON n.oid = c.relnamespace "
        "JOIN pg_sequence s ON s.seqrelid = c.oid "
        "JOIN pg_roles r ON r.oid = c.relowner "
        "WHERE ");

    if (deps->sequenceCount > 0) {
        appendStringInfo(&seqQuery, "c.oid IN (");
        for (int i = 0; i < deps->sequenceCount; i++) {
            if (i > 0)
                appendStringInfoString(&seqQuery, ", ");
            appendStringInfo(&seqQuery, "%u", deps->sequenceOids[i]);
        }
        appendStringInfo(&seqQuery, ") AND ");
    }
    

    appendStringInfo(&seqQuery,
        "n.nspname NOT IN ('pg_catalog','pg_toast','information_schema') "
        "ORDER BY n.nspname, c.relname");

    int ret = SPI_execute(seqQuery.data, true, 0);
    if (ret != SPI_OK_SELECT)
        elog(ERROR, "SPI_execute failed in generate_sequences_ddl_query");

    if (SPI_processed == 0) {
        pfree(seqQuery.data);
        return;
    }

    TupleDesc tupdesc = SPI_tuptable->tupdesc;
    appendStringInfoString(buf, "--\n-- Sequences\n--\n\n");

    for (int i = 0; i < SPI_processed; i++) {
        HeapTuple tuple = SPI_tuptable->vals[i];
        bool isNull[10];

        char* nspname = SPI_getvalue(tuple, tupdesc, 1);
        char* seqname = SPI_getvalue(tuple, tupdesc, 2);
        char* owner = SPI_getvalue(tuple, tupdesc, 3);

        Datum start_value_datum = SPI_getbinval(tuple, tupdesc, 4, &isNull[3]);
        Datum min_value_datum   = SPI_getbinval(tuple, tupdesc, 5, &isNull[4]);
        Datum max_value_datum   = SPI_getbinval(tuple, tupdesc, 6, &isNull[5]);
        Datum increment_datum   = SPI_getbinval(tuple, tupdesc, 7, &isNull[6]);
        Datum cycle_datum       = SPI_getbinval(tuple, tupdesc, 8, &isNull[7]);
        Datum cache_datum       = SPI_getbinval(tuple, tupdesc, 9, &isNull[8]);

        char* description = SPI_getvalue(tuple, tupdesc, 10);

        int64 start_value = !isNull[3] ? DatumGetInt64(start_value_datum) : 1;
        int64 min_value   = !isNull[4] ? DatumGetInt64(min_value_datum) : 1;
        int64 max_value   = !isNull[5] ? DatumGetInt64(max_value_datum) : 0;
        int64 increment   = !isNull[6] ? DatumGetInt64(increment_datum) : 1;
        bool cycle        = !isNull[7] ? DatumGetBool(cycle_datum) : false;
        int64 cache       = !isNull[8] ? DatumGetInt64(cache_datum) : 1;

        int64 last_value = 0;
        {
            StringInfoData lastValQuery;
            initStringInfo(&lastValQuery);
            appendStringInfo(&lastValQuery, "SELECT last_value FROM %s.%s", nspname, seqname);

            int ret2 = SPI_execute(lastValQuery.data, true, 1);
            if (ret2 == SPI_OK_SELECT && SPI_processed > 0) {
                HeapTuple t = SPI_tuptable->vals[0];
                bool isNullLast;
                Datum d = SPI_getbinval(t, SPI_tuptable->tupdesc, 1, &isNullLast);
                if (!isNullLast)
                    last_value = DatumGetInt64(d);
            }
            pfree(lastValQuery.data);
        }

        appendStringInfo(buf, "CREATE SEQUENCE %s.%s", nspname, seqname);
        appendStringInfo(buf, "\n    INCREMENT BY %ld", increment);
        appendStringInfo(buf, "\n    MINVALUE %ld", min_value);
        if (max_value != 0)
            appendStringInfo(buf, "\n    MAXVALUE %ld", max_value);
        else
            appendStringInfoString(buf, "\n    NO MAXVALUE");

        if (start_value != 1)
            appendStringInfo(buf, "\n    START WITH %ld", start_value);
        if (cache != 1)
            appendStringInfo(buf, "\n    CACHE %ld", cache);
        appendStringInfoString(buf, cycle ? "\n    CYCLE" : "\n    NO CYCLE");
        appendStringInfoString(buf, ";");

        char* owned_by = get_sequence_owned_by(nspname, seqname);
        if (owned_by) {
            appendStringInfo(buf, "\nALTER SEQUENCE %s.%s OWNED BY %s;", nspname, seqname, owned_by);
            pfree(owned_by);
        }

        if (last_value > 0)
            appendStringInfo(buf, "\nSELECT pg_catalog.setval('%s.%s', %ld, false);",
                             nspname, seqname, last_value);

        appendStringInfoString(buf, "\n\n");

        if (description)
            appendStringInfo(buf, "COMMENT ON SEQUENCE %s.%s IS '%s';\n\n", nspname, seqname, description);

        if (nspname) pfree(nspname);
        if (seqname) pfree(seqname);
        if (owner) pfree(owner);
        if (description) pfree(description);
    }

    pfree(seqQuery.data);
}



void generate_extensions_ddl_query(StringInfo buf, QueryDependencies* deps) {
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
                


                if (nspname)
                {
                    if (!strcmp(extname, "pg_ozerki")) {
                        appendStringInfoString(buf, " WITH SCHEMA public");
                    } else {
                        appendStringInfo(buf, " WITH SCHEMA %s", nspname);
                    }
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

void generate_tables_ddl_query(StringInfo buf, QueryDependencies* deps) {
    int ret;
    char *query;
    elog(LOG, "\n\nTABLE COUNT = %d\n\n", deps->tableCount);
   
    
    if (deps->tableCount > 0) {
        appendStringInfoString(buf, "--\n-- Tables\n--\n\n");
        
        SPITupleTable saved = *SPI_tuptable;
        TupleDesc tupdesc = saved.tupdesc;
        int tables_processed = SPI_processed;
        for (int i = 0; i < deps->tableCount; i++) {
            Oid tableOid = table_name_to_oid_internal(deps->tableNames[i]);
            
            
            generate_table_ddl(buf, tableOid);
            appendStringInfoString(buf, "\n\n");
            
        }
    } else {
        query = "SELECT c.oid "
            "FROM pg_class c "
            "JOIN pg_namespace n ON c.relnamespace = n.oid "
            "WHERE c.relkind = 'r' "
            "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "ORDER BY n.nspname, c.relname";
    
        ret = SPI_execute(query, true, 0);
        elog(LOG, "\n\nSPI PROCESSED %d\n\n", SPI_processed);
        if (ret == SPI_OK_SELECT && SPI_processed > 0)
        {
            appendStringInfoString(buf, "--\n-- Tables\n--\n\n");
            int tables_processed = SPI_processed;
            SPITupleTable saved = *SPI_tuptable;
            TupleDesc tupdesc = saved.tupdesc;
            for (int i = 0; i < tables_processed; i++)
            {
                HeapTuple tuple = saved.vals[i];
                bool isNull;
                elog(LOG, "\n\nTABLES PROCESSED %d\n\n", tables_processed);

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

    
}


Oid
table_name_to_oid_internal(const char *full_name)
{
    int     ret;
    Oid     relid = InvalidOid;
    bool    isnull;
    char   *sql;

    sql = psprintf(
        "SELECT to_regclass('%s')::oid",
        full_name
    );

   
    ret = SPI_execute(sql, true, 1);

    if (ret != SPI_OK_SELECT || SPI_processed != 1)
        elog(ERROR, "SPI_execute failed");

    Datum d = SPI_getbinval(
        SPI_tuptable->vals[0],
        SPI_tuptable->tupdesc,
        1,
        &isnull
    );

    if (isnull)
        elog(ERROR, "relation \"%s\" does not exist", full_name);

    relid = DatumGetObjectId(d);


    return relid;
}

char* escape_string(char *str) {
    if (!str) return pstrdup("");
    
    StringInfoData escaped;
    initStringInfo(&escaped);
    
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\'') {
            appendStringInfoString(&escaped, "''");
        } else {
            appendStringInfoChar(&escaped, str[i]);
        }
    }
    
    return escaped.data;
}

void generate_views_ddl_query(StringInfo buf, QueryDependencies* deps) {

    elog(LOG, "\n\n VIEWS COUNT = %d\n\n", deps->viewCount);
    if (deps->been_analyzed && deps->viewCount == 0) {
        return;
    }
    
    //SPI_execute("SET search_path = ''", false, 0);
    
    StringInfoData viewQuery;
    initStringInfo(&viewQuery);
    
    appendStringInfo(&viewQuery,
        "SELECT c.oid, n.nspname, c.relname, "
        "pg_catalog.pg_get_viewdef(c.oid, true) as definition, "
        "obj_description(c.oid, 'pg_class') as comment "
        "FROM pg_class c "
        "JOIN pg_namespace n ON c.relnamespace = n.oid "
        "WHERE ");

    if (deps->viewCount > 0) {
        appendStringInfo(&viewQuery, "c.oid IN (");
        for (int i = 0; i < deps->viewCount; i++) {
            if (i > 0) appendStringInfoString(&viewQuery, ", ");
            appendStringInfo(&viewQuery, "%u", deps->viewOids[i]);
        }
        appendStringInfo(&viewQuery, ") AND ");
    }
    
    appendStringInfo(&viewQuery,
        "c.relkind = 'v' "
        "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
        "ORDER BY "
        "CASE WHEN c.relname LIKE 'pg_%%' THEN 1 ELSE 0 END, "  
        "n.nspname, c.relname");
    
    char *query = viewQuery.data;
    elog(LOG, "\n\n GENERATE VIEW QUERY = %s\n\n", query);
    int ret = SPI_execute(query, true, 0);
    
    if (ret == SPI_OK_SELECT && SPI_processed > 0) {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        appendStringInfoString(buf, "--\n-- Views\n--\n\n");
        
        int views_exported = 0;
        
        for (int i = 0; i < SPI_processed; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull[5];
            
            Datum oid_datum = SPI_getbinval(tuple, tupdesc, 1, &isNull[0]);
            char* nspname = SPI_getvalue(tuple, tupdesc, 2);
            char* relname = SPI_getvalue(tuple, tupdesc, 3);
            char* definition = SPI_getvalue(tuple, tupdesc, 4);
            char* comment = SPI_getvalue(tuple, tupdesc, 5);
            
            if (!isNull[0] && !isNull[1] && nspname && !isNull[2] && relname && 
                !isNull[3] && definition) {
                
                Oid viewOid = DatumGetObjectId(oid_datum);
                
                appendStringInfo(buf, "CREATE OR REPLACE VIEW %s.%s AS\n%s;\n\n", 
                               nspname, relname, definition);
                views_exported++;
                
                if (!isNull[4] && comment && strlen(comment) > 0) {
                    char *escaped_comment = escape_string(comment);
                    appendStringInfo(buf, "COMMENT ON VIEW %s.%s IS '%s';\n\n", 
                                   nspname, relname, escaped_comment);
                    pfree(escaped_comment);
                }
                
                elog(LOG, "Exported view: %s.%s (OID: %u)", nspname, relname, viewOid);
            }
            
            if (nspname && !isNull[1]) pfree(nspname);
            if (relname && !isNull[2]) pfree(relname);
            if (definition && !isNull[3]) pfree(definition);
            if (comment && !isNull[4]) pfree(comment);
        }
        
    
    } else if (ret != SPI_OK_SELECT) {
        elog(WARNING, "Failed to get views DDL: SPI error %d", ret);
    }
    
    pfree(query);
    

}


void generate_indexes_ddl_query(StringInfo buf, QueryDependencies *deps) {
    
    if (deps->indexCount == 0 && deps->been_analyzed) {
        return;
    }
    int ret;
    char *query;
    Oid* tableOids = palloc(sizeof(Oid) * deps->tableCount);
    for (int i = 0; i < deps->tableCount; i++) {
        tableOids[i] = table_name_to_oid_internal(deps->tableNames[i]);
    }
    if (!deps->been_analyzed) {
        query = "SELECT n.nspname, c.relname as tablename, "
            "i.relname as indexname, "
            "pg_catalog.pg_get_indexdef(i.oid) as indexdef, "
            "i.oid as index_oid, "
            "x.indisunique, "
            "con.oid as constraint_oid "
            "FROM pg_index x "
            "JOIN pg_class i ON i.oid = x.indexrelid "
            "JOIN pg_class c ON c.oid = x.indrelid "
            "JOIN pg_namespace n ON n.oid = i.relnamespace "
            "LEFT JOIN pg_constraint con ON con.conindid = i.oid "  
            "WHERE i.relkind = 'i' "
            "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "AND NOT x.indisprimary "  
            "AND (con.oid IS NULL OR NOT x.indisunique) "  
            "ORDER BY n.nspname, c.relname, i.relname";
    }
    if (deps->been_analyzed && deps->indexCount > 0) {

        StringInfoData idxQuery;
        initStringInfo(&idxQuery);
        
        appendStringInfo(&idxQuery,
            "SELECT n.nspname, c.relname as tablename, "
            "i.relname as indexname, "
            "pg_catalog.pg_get_indexdef(i.oid) as indexdef, "
            "i.oid as index_oid, "
            "x.indisunique, "
            "con.oid as constraint_oid "
            "FROM pg_index x "
            "JOIN pg_class i ON i.oid = x.indexrelid "
            "JOIN pg_class c ON c.oid = x.indrelid "
            "JOIN pg_namespace n ON n.oid = i.relnamespace "
            "LEFT JOIN pg_constraint con ON con.conindid = i.oid "
            "WHERE i.oid IN (");
        
        for (int i = 0; i < deps->indexCount; i++) {
            if (i > 0) appendStringInfoString(&idxQuery, ", ");
            appendStringInfo(&idxQuery, "%u", deps->indexOids[i]);
        }
        
        appendStringInfo(&idxQuery,
            ") AND i.relkind = 'i' "
            "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "AND NOT x.indisprimary "
            "AND (con.oid IS NULL OR NOT x.indisunique) "
            "ORDER BY n.nspname, c.relname, i.relname");
        
        query = idxQuery.data;
    } else if (deps->been_analyzed && deps->tableCount > 0) {

        StringInfoData tableIdxQuery;
        initStringInfo(&tableIdxQuery);
        
        appendStringInfo(&tableIdxQuery,
            "SELECT n.nspname, c.relname as tablename, "
            "i.relname as indexname, "
            "pg_catalog.pg_get_indexdef(i.oid) as indexdef, "
            "i.oid as index_oid, "
            "x.indisunique, "
            "con.oid as constraint_oid "
            "FROM pg_index x "
            "JOIN pg_class i ON i.oid = x.indexrelid "
            "JOIN pg_class c ON c.oid = x.indrelid "
            "JOIN pg_namespace n ON n.oid = i.relnamespace "
            "LEFT JOIN pg_constraint con ON con.conindid = i.oid "
            "WHERE c.oid IN (");
        
        for (int i = 0; i < deps->tableCount; i++) {
            if (i > 0) appendStringInfoString(&tableIdxQuery, ", ");
            appendStringInfo(&tableIdxQuery, "%u", tableOids[i]);
        }
        
        appendStringInfo(&tableIdxQuery,
            ") AND i.relkind = 'i' "
            "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "AND NOT x.indisprimary "
            "AND (con.oid IS NULL OR NOT x.indisunique) "
            "ORDER BY n.nspname, c.relname, i.relname");
        
        query = tableIdxQuery.data;
    } 
    
    ret = SPI_execute(query, true, 0);
    
    if (ret == SPI_OK_SELECT && SPI_processed > 0) {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        appendStringInfoString(buf, "--\n-- Indexes\n--\n\n");
        
        for (int i = 0; i < SPI_processed; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull1, isNull2, isNull3, isNull4, isNull5, isNull6, isNull7;
            
            char* nspname = SPI_getvalue(tuple, tupdesc, 1);
            char* tablename = SPI_getvalue(tuple, tupdesc, 2);
            char* indexname = SPI_getvalue(tuple, tupdesc, 3);
            char* indexdef = SPI_getvalue(tuple, tupdesc, 4);
            Datum index_oid_datum = SPI_getbinval(tuple, tupdesc, 5, &isNull5);
            char* indisunique = SPI_getvalue(tuple, tupdesc, 6);
            char* constraint_oid = SPI_getvalue(tuple, tupdesc, 7);


            if (indisunique && constraint_oid && strcmp(indisunique, "t") == 0) {
                if (nspname) pfree(nspname);
                if (tablename) pfree(tablename);
                if (indexname) pfree(indexname);
                if (indexdef) pfree(indexdef);
                if (indisunique) pfree(indisunique);
                if (constraint_oid) pfree(constraint_oid);
                continue;
            }

            if (nspname && tablename && indexname && indexdef) {
                appendStringInfoString(buf, indexdef);
                appendStringInfoString(buf, ";\n\n");
                
                if (!isNull5) {
                    Oid indexOid = DatumGetObjectId(index_oid_datum);
                    char *index_comment = GetComment(indexOid, RelationRelationId, 0);
                    if (index_comment) {
                        appendStringInfo(buf, "COMMENT ON INDEX %s IS '%s';\n\n", 
                                       indexname, index_comment);
                        pfree(index_comment);
                    }
                }
            }
            

            if (nspname) pfree(nspname);
            if (tablename) pfree(tablename);
            if (indexname) pfree(indexname);
            if (indexdef) pfree(indexdef);
            if (indisunique) pfree(indisunique);
            if (constraint_oid) pfree(constraint_oid);
        }
    }
    

    if ((deps && deps->indexCount > 0) || (deps && deps->tableCount > 0)) {
        pfree(query);
    }
}


void
generate_schemas_ddl_query(StringInfo buf, QueryDependencies* deps) {
    int ret;
    char *query;
    

    if (deps && deps->schemaCount > 0) {

        StringInfoData schemaQuery;
        initStringInfo(&schemaQuery);
        
        appendStringInfo(&schemaQuery, 
            "SELECT n.oid, n.nspname, pg_catalog.pg_get_userbyid(n.nspowner) as owner, "
            "pg_catalog.obj_description(n.oid, 'pg_namespace') as description "
            "FROM pg_catalog.pg_namespace n "
            "WHERE n.nspname IN (");
        
        for (int i = 0; i < deps->schemaCount; i++) {
            if (i > 0) appendStringInfoString(&schemaQuery, ", ");
            appendStringInfo(&schemaQuery, "'%s'", deps->schemas[i]);
        }
        
        appendStringInfo(&schemaQuery, 
            ") AND n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' "
            "ORDER BY n.nspname");
        
        query = schemaQuery.data;
    } else {
        ereport(LOG, "\nPIZDAAA\n");
        query = "SELECT n.oid, n.nspname, pg_catalog.pg_get_userbyid(n.nspowner) as owner, "
                "pg_catalog.obj_description(n.oid, 'pg_namespace') as description "
                "FROM pg_catalog.pg_namespace n "
                "WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' "
                "ORDER BY n.nspname";
    }
    
    ret = SPI_execute(query, true, 0);
    if (ret == SPI_OK_SELECT && SPI_processed > 0) {
        appendStringInfoString(buf, "--\n-- Schemas\n--\n\n");

        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < SPI_processed; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull1, isNull2, isNull3, isNull4;
            
            char* nspname = SPI_getvalue(tuple, tupdesc, 2);
            char* owner = SPI_getvalue(tuple, tupdesc, 3);
            char* description = SPI_getvalue(tuple, tupdesc, 4);
            
            if (nspname && strcmp(nspname, "public") != 0) {
                appendStringInfo(buf, "CREATE SCHEMA %s", nspname);
                
                if (owner && strcmp(owner, GetUserNameFromId(GetUserId(), false)) != 0) {
                    appendStringInfo(buf, " AUTHORIZATION %s", owner);
                }
                
                appendStringInfoString(buf, ";\n");
                
                if (description) {
                    appendStringInfo(buf, "COMMENT ON SCHEMA %s IS '%s';\n", 
                                   nspname, description);
                }
                
                appendStringInfoString(buf, "\n");
            }
            

            if (nspname) pfree(nspname);
            if (owner) pfree(owner);
            if (description) pfree(description);
        }
    }
    

    if (deps && deps->schemaCount > 0) {
        pfree(query);
    }
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

void add_sequence_oid_to_deps(QueryDependencies *deps, Oid sequenceOid) {
    if (deps->sequenceCount == 0) {
        deps->sequenceOids = (Oid*)palloc(sizeof(Oid));
    } else {
        deps->sequenceOids = (Oid*)repalloc(deps->sequenceOids, (deps->sequenceCount + 1) * sizeof(Oid));
    }
    deps->sequenceOids[deps->sequenceCount++] = sequenceOid;
}

void
find_sequences_for_tables(QueryDependencies *deps)
{
    if (!deps || deps->tableCount == 0)
        return;

    StringInfoData sql;
    initStringInfo(&sql);

    
    appendStringInfo(&sql,
        "SELECT DISTINCT seq.oid "
        "FROM pg_class seq "
        "JOIN pg_sequence s ON s.seqrelid = seq.oid "
        "JOIN pg_depend d ON d.objid = seq.oid "
        "LEFT JOIN pg_class tbl_direct ON tbl_direct.oid = d.refobjid "
        "LEFT JOIN pg_attribute a ON a.attrelid = d.refobjid "
        "LEFT JOIN pg_class tbl_attr ON tbl_attr.oid = a.attrelid "
        "WHERE seq.relkind = 'S' "
        "AND d.deptype IN ('a','i') "
        "AND COALESCE(tbl_direct.oid, tbl_attr.oid) IN ("
        "SELECT c.oid "
        "FROM pg_class c "
        "JOIN pg_namespace n ON n.oid = c.relnamespace "
        "WHERE (n.nspname, c.relname) IN ("
    );

    for (int i = 0; i < deps->tableCount; i++)
    {
        char *name = pstrdup(deps->tableNames[i]);
        char *schema = strtok(name, ".");
        char *rel = strtok(NULL, ".");

        if (!schema || !rel)
            elog(ERROR, "Invalid table name: %s", deps->tableNames[i]);

        if (i > 0)
            appendStringInfoString(&sql, ", ");

        appendStringInfo(&sql,
            "('%s','%s')",
            schema,
            rel
        );
    }

    appendStringInfo(&sql, "))");

    int ret = SPI_execute(sql.data, true, 0);
    if (ret != SPI_OK_SELECT)
        elog(ERROR, "SPI_execute failed in find_sequences_for_tables");

    elog(LOG, "\nseq find query = %s\n", sql.data);
    if (SPI_processed == 0) {
        pfree(sql.data);
        return;
    }

    TupleDesc tupdesc = SPI_tuptable->tupdesc;

    for (int i = 0; i < SPI_processed; i++)
    {
        HeapTuple tuple = SPI_tuptable->vals[i];
        bool isnull;

        Datum d = SPI_getbinval(tuple, tupdesc, 1, &isnull);
        if (isnull)
            continue;

        Oid seqOid = DatumGetObjectId(d);

        bool exists = false;
        for (int j = 0; j < deps->sequenceCount; j++)
        {
            if (deps->sequenceOids[j] == seqOid)
            {
                exists = true;
                break;
            }
        }

        if (exists)
            continue;

        deps->sequenceOids = (deps->sequenceCount == 0)
            ? palloc(sizeof(Oid))
            : repalloc(deps->sequenceOids,
                       sizeof(Oid) * (deps->sequenceCount + 1));

        deps->sequenceOids[deps->sequenceCount++] = seqOid;
    }


    
}


void generate_functions_ddl_query(StringInfo buf, QueryDependencies* deps)
{
    int ret;
    char *query;


    query =
        "SELECT p.oid, n.nspname, p.proname, "
        "pg_catalog.pg_get_functiondef(p.oid) AS definition "
        "FROM pg_proc p "
        "JOIN pg_namespace n ON p.pronamespace = n.oid "
        "WHERE n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
        "AND NOT EXISTS ("
        "    SELECT 1 FROM pg_depend d "
        "    WHERE d.objid = p.oid "
        "      AND d.deptype = 'e'"      
        ") "
        "ORDER BY n.nspname, p.proname";

    ret = SPI_execute(query, true, 0);
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;

        appendStringInfoString(buf, "--\n-- Functions\n--\n\n");

        for (int i = 0; i < SPI_processed; i++)
        {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull = false;

            char *nspname    = SPI_getvalue(tuple, tupdesc, 2);
            char *proname    = SPI_getvalue(tuple, tupdesc, 3);
            char *definition = SPI_getvalue(tuple, tupdesc, 4);

            if (nspname && proname && definition)
            {
                appendStringInfo(buf, "%s;\n\n", definition);

                Datum oid_datum = SPI_getbinval(tuple, tupdesc, 1, &isNull);
                if (!isNull)
                {
                    Oid funcOid = DatumGetObjectId(oid_datum);
                    char *func_comment =
                        GetComment(funcOid, ProcedureRelationId, 0);

                    if (func_comment)
                    {
                        appendStringInfo(buf,
                            "COMMENT ON FUNCTION %s.%s IS '%s';\n\n",
                            nspname, proname, func_comment);
                        pfree(func_comment);
                    }
                }

                pfree(nspname);
                pfree(proname);
                pfree(definition);
            }
        }
    }
}


void generate_planner_settings_ddl(StringInfo buf) {
    int ret;
    const char *query;
    
    query = "SELECT name, setting, unit "
            "FROM pg_settings "
            "WHERE name IN ("
            "'seq_page_cost', "
            "'random_page_cost', "
            "'cpu_tuple_cost', "
            "'cpu_index_tuple_cost', "
            "'effective_cache_size', "
            "'work_mem', "
            "'default_statistics_target'"
            ") ORDER BY name";
    
    ret = SPI_execute(query, true, 0);
    
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        appendStringInfoString(buf, "--\n-- Planner Settings\n--\n\n");
        
        for (int i = 0; i < SPI_processed; i++)
        {
            HeapTuple tuple = SPI_tuptable->vals[i];
            
            char* name = SPI_getvalue(tuple, tupdesc, 1);
            char* setting = SPI_getvalue(tuple, tupdesc, 2);
            char* unit = SPI_getvalue(tuple, tupdesc, 3); 
            
            if (name && setting)
            {
                
                if (unit && strcmp(unit, "") != 0)
                {
                    
                    appendStringInfo(buf, "ALTER SYSTEM SET %s = '%s%s';\n", 
                                   name, setting, unit);
                }
                else
                {
                    appendStringInfo(buf, "ALTER SYSTEM SET %s = %s;\n", 
                                   name, setting);
                }
                
                pfree(name);
                pfree(setting);
                if (unit) pfree(unit);
            }
        }
        appendStringInfoString(buf, "\n");
    }
}