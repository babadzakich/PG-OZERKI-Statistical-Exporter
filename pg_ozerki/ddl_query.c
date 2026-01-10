#include "ddl.h"
#include "ozerki_utils.h"
#include "ddl_query.h"


void add_table_constraints_to_deps(QueryDependencies *deps) {
    if (!deps || deps->tableCount == 0) {
        return;
    }
    

    StringInfoData constrQuery;
    initStringInfo(&constrQuery);
    
    appendStringInfo(&constrQuery,
        "SELECT c.oid, c.contype, c.confrelid "
        "FROM pg_constraint c "
        "JOIN pg_class t ON t.oid = c.conrelid "
        "WHERE t.oid IN (");
    
    for (int i = 0; i < deps->tableCount; i++) {
        if (i > 0) appendStringInfoString(&constrQuery, ", ");
        appendStringInfo(&constrQuery, "%u", deps->tableOids[i]);
    }
    
    appendStringInfo(&constrQuery, ") AND c.contype IN ('p', 'f', 'c', 'u')");
    
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
                        if (deps->tableOids[j] == referencedTableOid) {
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
        appendStringInfo(&idxQuery, "%u", deps->tableOids[i]);
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
QueryDependencies* extract_tables_from_query_text(const char *query) {
    QueryDependencies *deps = (QueryDependencies*)palloc0(sizeof(QueryDependencies));
    

    deps->tableOids = NULL;
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
    
    char *query_copy = pstrdup(query);
    

    for (int i = 0; query_copy[i]; i++) {
        query_copy[i] = tolower(query_copy[i]);
    }
    
    elog(LOG, "Lowercased query: %s", query_copy);
    

    char *from_pos = strstr(query_copy, "from");
    
    if (!from_pos) {
        elog(LOG, "No FROM clause found in query");
        pfree(query_copy);
        return deps;
    }
    

    from_pos += 4;
    

    char *end_pos = from_pos;
    char *where_pos = strstr(from_pos, "where");
    char *group_pos = strstr(from_pos, "group");
    char *order_pos = strstr(from_pos, "order");
    char *limit_pos = strstr(from_pos, "limit");
    char *union_pos = strstr(from_pos, "union");
    char *intersect_pos = strstr(from_pos, "intersect");
    char *except_pos = strstr(from_pos, "except");
    

    char *closest_end = NULL;
    if (where_pos && (!closest_end || where_pos < closest_end)) closest_end = where_pos;
    if (group_pos && (!closest_end || group_pos < closest_end)) closest_end = group_pos;
    if (order_pos && (!closest_end || order_pos < closest_end)) closest_end = order_pos;
    if (limit_pos && (!closest_end || limit_pos < closest_end)) closest_end = limit_pos;
    if (union_pos && (!closest_end || union_pos < closest_end)) closest_end = union_pos;
    if (intersect_pos && (!closest_end || intersect_pos < closest_end)) closest_end = intersect_pos;
    if (except_pos && (!closest_end || except_pos < closest_end)) closest_end = except_pos;
    

    if (!closest_end) {
        closest_end = from_pos + strlen(from_pos);
    }
    

    size_t section_len = closest_end - from_pos;
    char *from_section = (char*)palloc(section_len + 1);
    strncpy(from_section, from_pos, section_len);
    from_section[section_len] = '\0';
    
    elog(LOG, "FROM section: '%s'", from_section);
    

    char *section_ptr = from_section;
    int table_num = 0;
    
    while (*section_ptr) {

        while (*section_ptr == ' ' || *section_ptr == '\t' || *section_ptr == '\n' || *section_ptr == '\r') {
            section_ptr++;
        }
        
        if (*section_ptr == '\0') break;
        

        bool is_join = false;
        

        if (strncmp(section_ptr, "join", 4) == 0) {
            char *after_join = section_ptr + 4;
            if (*after_join == ' ' || *after_join == '\t' || *after_join == '\n' || *after_join == '\r' || *after_join == '\0') {
                is_join = true;
                section_ptr = after_join;
                elog(LOG, "Found JOIN keyword");
                continue;
            }
        }
        

        if (strncmp(section_ptr, "inner", 5) == 0) {
            char *after_inner = section_ptr + 5;

            while (*after_inner == ' ' || *after_inner == '\t' || *after_inner == '\n' || *after_inner == '\r') {
                after_inner++;
            }
            if (strncmp(after_inner, "join", 4) == 0) {
                is_join = true;
                section_ptr = after_inner + 4;
                elog(LOG, "Found INNER JOIN keyword");
                continue;
            }
        }
        

        if (strncmp(section_ptr, "left", 4) == 0) {
            char *after_left = section_ptr + 4;
            while (*after_left == ' ' || *after_left == '\t' || *after_left == '\n' || *after_left == '\r') {
                after_left++;
            }
            if (strncmp(after_left, "join", 4) == 0) {
                is_join = true;
                section_ptr = after_left + 4;
                elog(LOG, "Found LEFT JOIN keyword");
                continue;
            }
        }
        

        if (strncmp(section_ptr, "right", 5) == 0) {
            char *after_right = section_ptr + 5;
            while (*after_right == ' ' || *after_right == '\t' || *after_right == '\n' || *after_right == '\r') {
                after_right++;
            }
            if (strncmp(after_right, "join", 4) == 0) {
                is_join = true;
                section_ptr = after_right + 4;
                elog(LOG, "Found RIGHT JOIN keyword");
                continue;
            }
        }
        

        if (strncmp(section_ptr, "full", 4) == 0) {
            char *after_full = section_ptr + 4;
            while (*after_full == ' ' || *after_full == '\t' || *after_full == '\n' || *after_full == '\r') {
                after_full++;
            }
            if (strncmp(after_full, "join", 4) == 0) {
                is_join = true;
                section_ptr = after_full + 4;
                elog(LOG, "Found FULL JOIN keyword");
                continue;
            }
        }
        

        if (strncmp(section_ptr, "cross", 5) == 0) {
            char *after_cross = section_ptr + 5;
            while (*after_cross == ' ' || *after_cross == '\t' || *after_cross == '\n' || *after_cross == '\r') {
                after_cross++;
            }
            if (strncmp(after_cross, "join", 4) == 0) {
                is_join = true;
                section_ptr = after_cross + 4;
                elog(LOG, "Found CROSS JOIN keyword");
                continue;
            }
        }
        

        if (strncmp(section_ptr, "natural", 7) == 0) {
            char *after_natural = section_ptr + 7;
            while (*after_natural == ' ' || *after_natural == '\t' || *after_natural == '\n' || *after_natural == '\r') {
                after_natural++;
            }
            if (strncmp(after_natural, "join", 4) == 0) {
                is_join = true;
                section_ptr = after_natural + 4;
                elog(LOG, "Found NATURAL JOIN keyword");
                continue;
            }
        }
        

        char *table_start = section_ptr;
        

        while (*section_ptr && 
               *section_ptr != ' ' && 
               *section_ptr != '\t' && 
               *section_ptr != '\n' && 
               *section_ptr != '\r' && 
               *section_ptr != ',' && 
               !(strncmp(section_ptr, " on", 3) == 0 && 
                 (section_ptr[3] == ' ' || section_ptr[3] == '\t' || section_ptr[3] == '('))) {
            section_ptr++;
        }
        
        if (section_ptr == table_start) {
            continue;
        }
        

        size_t table_name_len = section_ptr - table_start;
        char *table_name = (char*)palloc(table_name_len + 1);
        strncpy(table_name, table_start, table_name_len);
        table_name[table_name_len] = '\0';
        
        elog(LOG, "Table %d raw name: '%s'", ++table_num, table_name);
        

        char *clean_name = table_name;
        if (clean_name[0] == '"') {
            clean_name++;
            if (clean_name[strlen(clean_name)-1] == '"') {
                clean_name[strlen(clean_name)-1] = '\0';
            }
        }
        

        char *schema_name = "public"; 
        char *table_name_only = clean_name;
        char *dot_pos = strchr(clean_name, '.');
        
        if (dot_pos) {
            *dot_pos = '\0';
            schema_name = clean_name;
            table_name_only = dot_pos + 1;
            

            if (schema_name[0] == '"' && schema_name[strlen(schema_name)-1] == '"') {
                schema_name[strlen(schema_name)-1] = '\0';
                schema_name++;
            }
        }
        

        if (table_name_only[0] == '"' && table_name_only[strlen(table_name_only)-1] == '"') {
            table_name_only[strlen(table_name_only)-1] = '\0';
            table_name_only++;
        }
        
        elog(LOG, "Parsed table: schema='%s', table='%s'", schema_name, table_name_only);
        

        char *find_table_query = psprintf(
            "SELECT c.oid, c.relkind FROM pg_class c "
            "JOIN pg_namespace n ON c.relnamespace = n.oid "
            "WHERE n.nspname = '%s' AND c.relname = '%s' "
            "AND c.relkind IN ('r', 'm', 'v', 'p')",
            schema_name, table_name_only);
        
        elog(LOG, "Executing query: %s", find_table_query);
        
        int ret = SPI_execute(find_table_query, true, 0);
        
        if (ret != SPI_OK_SELECT) {
            elog(LOG, "SPI_execute failed with code: %d", ret);
        } else if (SPI_processed == 0) {
            elog(LOG, "Table not found: %s.%s", schema_name, table_name_only);
        } else {
            elog(LOG, "Found %d rows for table %s.%s", SPI_processed, schema_name, table_name_only);
        }
        
        pfree(find_table_query);
        
        if (ret == SPI_OK_SELECT && SPI_processed > 0) {
            HeapTuple tuple = SPI_tuptable->vals[0];
            bool isNull1, isNull2;
            
            Datum oid_datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc, 1, &isNull1);
            char *relkind = SPI_getvalue(tuple, SPI_tuptable->tupdesc, 2);
            
            if (!isNull1) {
                Oid objOid = DatumGetObjectId(oid_datum);
                elog(LOG, "Found OID: %u, relkind: %s", objOid, relkind ? relkind : "NULL");
                

                bool schema_exists = false;
                for (int i = 0; i < deps->schemaCount; i++) {
                    if (strcmp(deps->schemas[i], schema_name) == 0) {
                        schema_exists = true;
                        break;
                    }
                }
                
                if (!schema_exists) {
                    if (deps->schemaCount == 0) {
                        deps->schemas = (char**)palloc(sizeof(char*));
                    } else {
                        deps->schemas = (char**)repalloc(deps->schemas, (deps->schemaCount + 1) * sizeof(char*));
                    }
                    deps->schemas[deps->schemaCount] = pstrdup(schema_name);
                    deps->schemaCount++;
                    elog(LOG, "Added schema: %s", schema_name);
                }
                

                if (relkind) {
                    switch (relkind[0]) {
                        case 'r': 
                        case 'm': 
                        case 'p': 
                            if (deps->tableCount == 0) {
                                deps->tableOids = (Oid*)palloc(sizeof(Oid));
                            } else {
                                deps->tableOids = (Oid*)repalloc(deps->tableOids, (deps->tableCount + 1) * sizeof(Oid));
                            }
                            deps->tableOids[deps->tableCount++] = objOid;
                            elog(LOG, "Added table OID: %u", objOid);
                            break;
                            
                        case 'v': 
                            if (deps->viewCount == 0) {
                                deps->viewOids = (Oid*)palloc(sizeof(Oid));
                            } else {
                                deps->viewOids = (Oid*)repalloc(deps->viewOids, (deps->viewCount + 1) * sizeof(Oid));
                            }
                            deps->viewOids[deps->viewCount++] = objOid;
                            elog(LOG, "Added view OID: %u", objOid);
                            break;
                    }
                }
                
                if (relkind) pfree(relkind);
            }
        }
        
        pfree(table_name);
        

        while (*section_ptr == ' ' || *section_ptr == '\t' || *section_ptr == '\n' || *section_ptr == '\r') {
            section_ptr++;
        }
        

        if (*section_ptr && strncmp(section_ptr, "as", 2) == 0 && 
            (section_ptr[2] == ' ' || section_ptr[2] == '\t')) {
            section_ptr += 2;
            while (*section_ptr == ' ' || *section_ptr == '\t') section_ptr++;

            while (*section_ptr && 
                   *section_ptr != ' ' && 
                   *section_ptr != '\t' && 
                   *section_ptr != ',' && 
                   *section_ptr != '\n' && 
                   *section_ptr != '\r') {
                section_ptr++;
            }
        } else if (*section_ptr && *section_ptr != ',' && *section_ptr != '\0') {

            char *check = section_ptr;
            while (*check && 
                   *check != ' ' && 
                   *check != '\t' && 
                   *check != ',' && 
                   *check != '\n' && 
                   *check != '\r' && 
                   !(strncmp(check, " on", 3) == 0 && 
                     (check[3] == ' ' || check[3] == '\t' || check[3] == '('))) {
                check++;
            }
            

            if (*check && strncmp(check, " on", 3) != 0) {
                section_ptr = check;
            }
        }
        

        while (*section_ptr == ' ' || *section_ptr == '\t' || *section_ptr == '\n' || *section_ptr == '\r') {
            section_ptr++;
        }
        

        if (*section_ptr && strncmp(section_ptr, "on", 2) == 0 && 
            (section_ptr[2] == ' ' || section_ptr[2] == '\t' || section_ptr[2] == '(')) {
            elog(LOG, "Found ON clause, skipping");
            section_ptr += 2;

            int paren_count = 0;
            while (*section_ptr && !(*section_ptr == ',' && paren_count == 0)) {
                if (*section_ptr == '(') paren_count++;
                else if (*section_ptr == ')') paren_count--;
                section_ptr++;
            }
        }
        

        if (*section_ptr == ',') {
            section_ptr++;
        }
    }
    
    pfree(from_section);
    pfree(query_copy);
    
    elog(LOG, "Finished analysis. Found %d tables, %d schemas", 
         deps->tableCount, deps->schemaCount);
    
    return deps;
}

QueryDependencies* analyze_query_dependencies(const char *query) {
    QueryDependencies *deps = (QueryDependencies*)palloc0(sizeof(QueryDependencies));
    int ret; 
    deps = extract_tables_from_query_text(query);
    find_sequences_for_tables(deps);
    add_table_constraints_to_deps(deps);
    add_table_indexes_to_deps(deps);
    return deps;
}



void generate_constraints_ddl_query(StringInfo buf, QueryDependencies *deps) {
    int ret;
    char *query;
    elog(LOG, "\n\nCONSTR COUNT = %d\n\n", deps->constraintCount);
    if (deps && deps->constraintCount > 0) {

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
            "WHERE c.oid IN (");
        
        for (int i = 0; i < deps->constraintCount; i++) {
            if (i > 0) appendStringInfoString(&constrQuery, ", ");
            appendStringInfo(&constrQuery, "%u", deps->constraintOids[i]);
        }
        
        appendStringInfo(&constrQuery,
            ") AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "AND c.contype IN ('f', 'c', 'u') "
            "ORDER BY n.nspname, t.relname, c.contype, c.conname");
        
        query = constrQuery.data;
    } 
    else {
        return;
    }
    
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
    
    if (deps->sequenceCount == 0) {
        return;
    }
    int ret;
    char *query;
    
    elog(LOG, "\n\n SEQUENCES FOR GEN = %d\n\n", deps->sequenceCount);
    if (deps && deps->sequenceCount > 0) {
        StringInfoData seqQuery;
        initStringInfo(&seqQuery);
        
        appendStringInfo(&seqQuery,
            "SELECT schemaname, sequencename, "
            "sequenceowner, start_value, min_value, max_value, "
            "increment_by, cycle, cache_size, last_value, "
            "pg_catalog.obj_description(pg_sequence.seqrelid, 'pg_class') as description "
            "FROM pg_sequences "
            "JOIN pg_sequence ON pg_sequence.seqrelid = pg_sequences.sequencename::regclass "
            "WHERE pg_sequence.seqrelid IN (");
        
        for (int i = 0; i < deps->sequenceCount; i++) {
            if (i > 0) appendStringInfoString(&seqQuery, ", ");
            appendStringInfo(&seqQuery, "%u", deps->sequenceOids[i]);
                elog(LOG, "\n\n SEQUENCE COUNT = %d\n\n", deps->sequenceOids[i]);

        }
        
        appendStringInfo(&seqQuery,
            ") AND schemaname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "ORDER BY schemaname, sequencename");
        query = seqQuery.data;
    } else {
        query = "SELECT schemaname, sequencename, "
                "sequenceowner, start_value, min_value, max_value, "
                "increment_by, cycle, cache_size, last_value, "
                "pg_catalog.obj_description(pg_sequence.seqrelid, 'pg_class') as description "
                "FROM pg_sequences "
                "JOIN pg_sequence ON pg_sequence.seqrelid = pg_sequences.sequencename::regclass "
                "WHERE schemaname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
                "ORDER BY schemaname, sequencename";
    }
    
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
    if (deps && deps->tableCount > 0) {

        StringInfoData tableQuery;
        initStringInfo(&tableQuery);
        
        appendStringInfo(&tableQuery, "SELECT c.oid "
                        "FROM pg_class c "
                        "JOIN pg_namespace n ON c.relnamespace = n.oid "
                        "WHERE c.oid IN (");
        
        for (int i = 0; i < deps->tableCount; i++) {
            if (i > 0) appendStringInfoString(&tableQuery, ", ");
            appendStringInfo(&tableQuery, "%u", deps->tableOids[i]);
        }
        
        appendStringInfo(&tableQuery, ") "
                        "AND c.relkind = 'r' "
                        "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
                        "ORDER BY n.nspname, c.relname");
        
        query = tableQuery.data;
    } else if (deps && deps->schemaCount > 0) {

        StringInfoData schemaQuery;
        initStringInfo(&schemaQuery);
        
        appendStringInfo(&schemaQuery, "SELECT c.oid "
                        "FROM pg_class c "
                        "JOIN pg_namespace n ON c.relnamespace = n.oid "
                        "WHERE n.nspname IN (");
        
        for (int i = 0; i < deps->schemaCount; i++) {
            if (i > 0) appendStringInfoString(&schemaQuery, ", ");
            appendStringInfo(&schemaQuery, "'%s'", deps->schemas[i]);
        }
        
        appendStringInfo(&schemaQuery, ") "
                        "AND c.relkind = 'r' "
                        "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
                        "ORDER BY n.nspname, c.relname");
        
        query = schemaQuery.data;
    } else {
        query = "SELECT c.oid "
                "FROM pg_class c "
                "JOIN pg_namespace n ON c.relnamespace = n.oid "
                "WHERE c.relkind = 'r' "
                "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
                "ORDER BY n.nspname, c.relname";
    }
    
    ret = SPI_execute(query, true, 0);
    
    if (ret == SPI_OK_SELECT && SPI_processed > 0) {
        appendStringInfoString(buf, "--\n-- Tables\n--\n\n");
        
        SPITupleTable saved = *SPI_tuptable;
        TupleDesc tupdesc = saved.tupdesc;
        int tables_processed = SPI_processed;
        for (int i = 0; i < tables_processed; i++) {
            HeapTuple tuple = saved.vals[i];
            bool isNull;
            elog(LOG, "\n\nTABLES PROCESSED %d\n\n", tables_processed);
            Datum oid_datum = SPI_getbinval(tuple, tupdesc, 1, &isNull);
            if (!isNull) {
                Oid tableOid = DatumGetObjectId(oid_datum);
                generate_table_ddl(buf, tableOid);
                appendStringInfoString(buf, "\n\n");
            }
        }
    }
    

    if (deps && (deps->tableCount > 0 || deps->schemaCount > 0)) {
        pfree(query);
    }
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
    if (!deps || deps->viewCount == 0) {
        return;
    }
    
    SPI_execute("SET search_path = ''", false, 0);
    
    StringInfoData viewQuery;
    initStringInfo(&viewQuery);
    
    appendStringInfo(&viewQuery,
        "SELECT c.oid, n.nspname, c.relname, "
        "pg_catalog.pg_get_viewdef(c.oid, true) as definition, "
        "obj_description(c.oid, 'pg_class') as comment "
        "FROM pg_class c "
        "JOIN pg_namespace n ON c.relnamespace = n.oid "
        "WHERE c.oid IN (");
    
    for (int i = 0; i < deps->viewCount; i++) {
        if (i > 0) appendStringInfoString(&viewQuery, ", ");
        appendStringInfo(&viewQuery, "%u", deps->viewOids[i]);
    }
    
    appendStringInfo(&viewQuery,
        ") AND c.relkind = 'v' "
        "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
        "ORDER BY "
        "CASE WHEN c.relname LIKE 'pg_%' THEN 1 ELSE 0 END, "  
        "n.nspname, c.relname");
    
    char *query = viewQuery.data;
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
    int ret;
    char *query;
    
    if (deps && deps->indexCount > 0) {

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
    } else if (deps && deps->tableCount > 0) {

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
            appendStringInfo(&tableIdxQuery, "%u", deps->tableOids[i]);
        }
        
        appendStringInfo(&tableIdxQuery,
            ") AND i.relkind = 'i' "
            "AND n.nspname NOT IN ('pg_catalog', 'pg_toast', 'information_schema') "
            "AND NOT x.indisprimary "
            "AND (con.oid IS NULL OR NOT x.indisunique) "
            "ORDER BY n.nspname, c.relname, i.relname");
        
        query = tableIdxQuery.data;
    } else {
        return;
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

void find_sequences_for_tables(QueryDependencies *deps) {
    if (!deps || deps->tableCount == 0) {
        return;
    }
    

    StringInfoData seqQuery;
    initStringInfo(&seqQuery);
    
    appendStringInfo(&seqQuery,
        "SELECT DISTINCT s.seqrelid "
        "FROM pg_depend d "
        "JOIN pg_class c ON c.oid = d.objid "
        "JOIN pg_sequence s ON s.seqrelid = c.oid "
        "JOIN pg_class t ON t.oid = d.refobjid "
        "WHERE d.deptype = 'a' "  
        "AND d.classid = 'pg_class'::regclass::oid "
        "AND d.refclassid = 'pg_class'::regclass::oid "
        "AND c.relkind = 'S' "  
        "AND t.oid IN (");
    
    for (int i = 0; i < deps->tableCount; i++) {
        if (i > 0) appendStringInfoString(&seqQuery, ", ");
        appendStringInfo(&seqQuery, "%u", deps->tableOids[i]);
        elog(LOG, "\n\n TABLE WITH SEQUENCE OID = %d\n\n", deps->tableOids[i]);
    }
    
    appendStringInfo(&seqQuery, ")");
    
    int ret = SPI_execute(seqQuery.data, true, 0);
    elog(LOG, "\n\n SEQUENCES FOUND = %d\n\n", SPI_processed);
    if (ret == SPI_OK_SELECT && SPI_processed > 0) {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < SPI_processed; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull;
            
            Datum seqOid_datum = SPI_getbinval(tuple, tupdesc, 1, &isNull);
            
            if (!isNull) {
                Oid seqOid = DatumGetObjectId(seqOid_datum);
                

                bool exists = false;
                for (int j = 0; j < deps->sequenceCount; j++) {
                    if (deps->sequenceOids[j] == seqOid) {
                        exists = true;
                        break;
                    }
                }
                
                if (!exists) {

                    if (deps->sequenceCount == 0) {
                        deps->sequenceOids = (Oid*)palloc(sizeof(Oid));
                    } else {
                        deps->sequenceOids = (Oid*)repalloc(deps->sequenceOids, 
                                                          (deps->sequenceCount + 1) * sizeof(Oid));
                    }
                    deps->sequenceOids[deps->sequenceCount++] = seqOid;
                    
                    elog(LOG, "Added sequence OID %u for table", seqOid);
                }
            }
        }
    }
    
    pfree(seqQuery.data);
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