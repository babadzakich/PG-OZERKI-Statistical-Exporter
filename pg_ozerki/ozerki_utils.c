#include "ozerki_utils.h"

char* add_schema_to_constraint(char* condef, char* schemaname) {
    StringInfoData buf;
    
    initStringInfo(&buf);
    
    const char *pos = condef;
    
    while (*pos)
    {
        if ((strncmp(pos, "REFERENCES ", 11) == 0) ||
            (strncmp(pos, "ON TABLE ", 9) == 0) ||
            (strncmp(pos, "USING ", 6) == 0))
        {
            if (strncmp(pos, "REFERENCES ", 11) == 0)
            {
                appendStringInfoString(&buf, "REFERENCES ");
                pos += 11;
            }
            else if (strncmp(pos, "ON TABLE ", 9) == 0)
            {
                appendStringInfoString(&buf, "ON TABLE ");
                pos += 9;
            }
            else if (strncmp(pos, "USING ", 6) == 0)
            {
                appendStringInfoString(&buf, "USING ");
                pos += 6;
            }
            
            while (*pos && isspace((unsigned char)*pos))
                pos++;
            
            const char *table_start = pos;
            const char *table_end = pos;
            
            while (*table_end && !isspace((unsigned char)*table_end) && 
                   *table_end != '(' && *table_end != ')' && *table_end != ',')
                table_end++;
            
            bool has_schema = false;
            for (const char *p = table_start; p < table_end; p++)
            {
                if (*p == '.')
                {
                    has_schema = true;
                    break;
                }
            }
            
            if (!has_schema && (table_end - table_start) > 0)
            {
                appendStringInfo(&buf, "%s.", schemaname);
            }
            
            appendStringInfo(&buf, "%.*s", (int)(table_end - table_start), table_start);
            pos = table_end;
        }
        else
        {
            appendStringInfoChar(&buf, *pos);
            pos++;
        }
    }
    
    return buf.data;
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


char* get_table_check_constraints(Oid tableOid) {
    int ret;
    char *query;
    StringInfoData buf;
    
    initStringInfo(&buf);
    
    query = "SELECT c.conname, pg_catalog.pg_get_constraintdef(c.oid) as condef "
            "FROM pg_constraint c "
            "WHERE c.conrelid = $1 AND c.contype = 'c' "
            "AND c.connoinherit "
            "ORDER BY c.conname";
    
    ret = SPI_execute_with_args(query, 1, 
                               (Oid[]) {OIDOID},
                               (Datum[]) {tableOid},
                               (bool[]) {false},
                               true, 0);
    
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < SPI_processed; i++)
        {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isNull1, isNull2;
            
            char* conname = SPI_getvalue(tuple, tupdesc, 1);
            char* condef = SPI_getvalue(tuple, tupdesc, 2);
            
            if (conname && condef)
            {
                
                appendStringInfo(&buf, ",\n    CONSTRAINT %s %s", conname, condef);
                
                pfree(conname);
                pfree(condef);
            }
        }
    }
    
    if (buf.len > 0)
        return buf.data;
    else
    {
        pfree(buf.data);
        return NULL;
    }
}


char* get_primary_key_constraint(Oid tableOid) {
    int ret;
    char *query;
    char *pk_constraint = NULL;
    
    query = "SELECT conname, "
            "pg_catalog.pg_get_constraintdef(oid) as condef "
            "FROM pg_constraint "
            "WHERE conrelid = $1 AND contype = 'p'";
    
    ret = SPI_execute_with_args(query, 1, 
                               (Oid[]) {OIDOID},
                               (Datum[]) {tableOid},
                               (bool[]) {false},
                               true, 0);
    
    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        HeapTuple tuple = SPI_tuptable->vals[0];
        bool isNull1, isNull2;
        
        char* conname = SPI_getvalue(tuple, tupdesc, 1);
        char* condef = SPI_getvalue(tuple, tupdesc, 2);
        
        if (conname && condef)
        {
            
            
            pk_constraint = psprintf("CONSTRAINT %s %s", conname, condef);
            
            pfree(conname);
            pfree(condef);
        }
    }
    
    return pk_constraint;
}