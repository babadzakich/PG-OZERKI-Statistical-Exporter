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

void generate_table_ddl(StringInfo buf, Oid tableOid);
void generate_tables_ddl(StringInfo buf);
void generate_views_ddl(StringInfo buf);
void generate_indexes_ddl(StringInfo buf);
void generate_extensions_ddl(StringInfo buf);
void generate_sequences_ddl(StringInfo buf);
void generate_constraints_ddl(StringInfo buf);