#include "ddl.h"
#include "ozerki_utils.h"

typedef struct {
   Oid *tableOids;
    int tableCount;
    Oid *viewOids;
    int viewCount;
    Oid *functionOids;
    int functionCount;
    Oid *sequenceOids;
    int sequenceCount;
    Oid *indexOids;        // Добавляем для индексов
    int indexCount;
    Oid *constraintOids;   // Добавляем для констрейнтов
    int constraintCount;
    char **schemas;
    int schemaCount;
} QueryDependencies;

QueryDependencies* analyze_query_dependencies(const char *query);
void generate_tables_ddl_query(StringInfo buf, QueryDependencies* deps);
QueryDependencies* extract_tables_from_query_text(const char *query);
void generate_schemas_ddl_query(StringInfo buf, QueryDependencies* deps);

void generate_functions_ddl_query(StringInfo buf, QueryDependencies* deps);

void generate_extensions_ddl_query(StringInfo buf, QueryDependencies* deps);
void generate_sequences_ddl_query(StringInfo buf, QueryDependencies* deps);
void generate_indexes_ddl_query(StringInfo buf, QueryDependencies* deps);
void generate_constraints_ddl_query(StringInfo buf, QueryDependencies* deps);
void generate_views_ddl_query(StringInfo buf, QueryDependencies* deps);