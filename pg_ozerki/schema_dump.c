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

PG_MODULE_MAGIC;



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

        generate_constraints_ddl(&buf);
    }
        
    
    
    elog(LOG, "%s\n\n", buf.data);
    
    text* ret = cstring_to_text(buf.data);
    //SPI_finish();
    PG_RETURN_TEXT_P(ret);
}

