#include "ddl.h"
#include "ozerki_utils.h"



void generate_constraints_ddl(StringInfo buf) {
    int ret;
    char *query;
    
    query = "SELECT n.nspname, t.relname as tablename, "
            "c.conname, c.contype, "
            "pg_catalog.pg_get_constraintdef(c.oid) as condef, "
            "c.convalidated, c.conislocal, c.coninhcount, "
            "c.oid as conoid "
            "FROM pg_constraint c "
            "JOIN pg_class t ON t.oid = c.conrelid "
            "JOIN pg_namespace n ON n.oid = t.relnamespace "
            "WHERE n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "AND c.contype IN ('f', 'c', 'u') "  
            "ORDER BY "
            "CASE c.contype "
            "  WHEN 'u' THEN 1 " 
            "  WHEN 'f' THEN 2 "  
            "  WHEN 'c' THEN 3 "  
            "END, "
            "n.nspname, t.relname, c.conname";
    
    ret = SPI_execute(query, true, 0);
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        appendStringInfoString(buf, "--\n-- Constraints\n--\n\n");
        for (int i = 0; i < SPI_processed; i++)
        {
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
            
            if (nspname && tablename && conname && contype && condef)
            {
                
                bool convalidated = !isNull[5] ? DatumGetBool(convalidated_datum) : true;
                bool conislocal = !isNull[6] ? DatumGetBool(conislocal_datum) : true;
                int32 coninhcount = !isNull[7] ? DatumGetInt32(coninhcount_datum) : 0;
                
                if (coninhcount > 0)
                    continue;
                
                if (!convalidated)
                {
                    appendStringInfo(buf, "-- Constraint %s.%s is NOT VALID and needs validation\n", 
                                   tablename, conname);
                    continue;
                }
                
                char *constraint_type;
                switch (contype[0])
                {
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
                
                appendStringInfoString(buf, schema_added);

                if (!convalidated)
                {
                    appendStringInfoString(buf, " NOT VALID");
                }
                
                appendStringInfoString(buf, ";\n");
                
                Oid conoid = DatumGetObjectId(conoid_datum);
                char *constraint_comment = GetComment(conoid, ConstraintRelationId, 0);
                if (constraint_comment)
                {
                    appendStringInfo(buf, "COMMENT ON CONSTRAINT %s ON %s.%s IS '%s';\n", 
                                   conname, nspname, tablename, constraint_comment);
                    pfree(constraint_comment);
                }
                
                appendStringInfoString(buf, "\n");
                
                pfree(nspname);
                pfree(tablename);
                pfree(conname);
                pfree(condef);
            }
        }
    }
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
        
        int sequences_processed = SPI_processed;
        SPITupleTable saved = *SPI_tuptable;
        TupleDesc tupdesc = saved.tupdesc;
        appendStringInfoString(buf, "--\n-- Sequences\n--\n\n");
        
        for (int i = 0; i < sequences_processed; i++)
        {
            HeapTuple tuple = saved.vals[i];
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
            
            
            elog(LOG, "\n\n SEQUENCE %d NSPNAME = %s SEQUENCE NAME = %s\n\n", i, nspname, seqname);
            
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
                
                //ereport(LOG, "%s\n\n", nspname);

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
    elog(LOG, "\n\nSPI_processed = ", SPI_processed);
}
 
void generate_views_ddl(StringInfo buf) {
    int ret;
    char *query;
    SPI_execute("SET search_path = ''", false, 0);

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
     
            char* indisunique = SPI_getvalue(tuple, tupdesc, 6);
            char* constraint_oid = SPI_getvalue(tuple, tupdesc, 7);

            if (indisunique && constraint_oid)
                continue;

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
            
            if (nspname && strcmp(nspname, "public") != 0)
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


void generate_functions_ddl(StringInfo buf)
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