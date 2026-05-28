#include "ddl.h"
#include "ozerki_utils.h"


void analyze_query_dependencies(const char *query, QueryDependencies* deps);
void generate_tables_ddl_query(StringInfo buf, QueryDependencies* deps);
void extract_tables_from_query_text(const char *query, QueryDependencies* deps);
void generate_schemas_ddl_query(StringInfo buf, QueryDependencies* deps);

void generate_functions_ddl_query(StringInfo buf, QueryDependencies* deps);

void generate_extensions_ddl_query(StringInfo buf, QueryDependencies* deps);
void generate_sequences_ddl_query(StringInfo buf, QueryDependencies* deps);
void generate_indexes_ddl_query(StringInfo buf, QueryDependencies* deps);
void generate_constraints_ddl_query(StringInfo buf, QueryDependencies* deps);
void generate_views_ddl_query(StringInfo buf, QueryDependencies* deps);

Oid table_name_to_oid_internal(const char *full_name);

QueryDependencies* init_deps();