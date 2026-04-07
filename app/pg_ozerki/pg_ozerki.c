#include <stdio.h>
#include "pg_ozerki.h"
#include "libpq/libpq-fs.h"
#include "libpq-fe.h"
#include "fe_utils/option_utils.h"
#include "fe_utils/string_utils.h"
#include "getopt_long.h" 
#include "catalog/pg_class.h"

#define BATCH_SIZE 1024

SimpleStringList table_include_patterns = {NULL, NULL};
SimpleOidList table_include_oids = {NULL, NULL};

static SimpleStringList schema_include_patterns = {NULL, NULL};
static SimpleOidList schema_include_oids = {NULL, NULL};

int	strict_names = 0;

bool no_checks = false;
bool no_exts = false;

static void
expand_schema_name_patterns(Archive *fout,
							SimpleStringList *patterns,
							SimpleOidList *oids,
							bool strict_names);

static void prohibit_crossdb_refs(PGconn *conn, const char *dbname,
								  const char *pattern);

static void
setup_connection(Archive *AH, const char *dumpencoding,
				 const char *dumpsnapshot, char *use_role);
static void
set_restrict_relation_kind(Archive *AH, const char *value);

static void
setupDumpWorker(Archive *AH);


static void
getDependencies(Archive *fout);
static bool
_tocEntryIsACL(TocEntry *te);
static void
addBoundaryDependencies(DumpableObject **dobjs, int numObjs,
						DumpableObject *boundaryObjs);
static void
StrictNamesCheck(RestoreOptions *ropt);

TocEntry *
getTocEntryByDumpId(ArchiveHandle *AH, DumpId id);

static int
_tocEntryRequired(TocEntry *te, teSection curSection, ArchiveHandle *AH);

static void
buildTocEntryArrays(ArchiveHandle *AH);

void
expand_table_name_patterns(Archive *fout,
						   SimpleStringList *patterns, SimpleOidList *oids,
						   bool strict_names, bool with_child_tables);

static bool have_extra_float_digits = false;
static int	extra_float_digits;

static char *
sanitize_line(const char *str, bool want_hyphen)
{
	char	   *result;
	char	   *s;

	if (!str)
		return pg_strdup(want_hyphen ? "-" : "");

	result = pg_strdup(str);

	for (s = result; *s != '\0'; s++)
	{
		if (*s == '\n' || *s == '\r')
			*s = ' ';
	}

	return result;
}

char* planner_settings = NULL;

typedef enum {
	DBNAME,
	USERNAME,
	HOST,
	PORT,
	SCHEMA_FILE,
	QUERY,
	EXPLAINFILE,
	EXPLAINFILE_ANALZYE,
	STATS_FILE,
	NO_CHECKS,
	NO_EXTS,
	QUERY_FILE
} getopt_params;

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

void find_views_for_tables(PGconn* conn,QueryDependencies *deps, char* query_text)
{
    if (!deps || deps->tableCount == 0){
        return;
    }


    PQExpBuffer sql = createPQExpBuffer();
    PGresult *res;

	char *clean_query = pg_strdup(query_text);
    size_t len = strlen(clean_query);

    while (len > 0 && (clean_query[len - 1] == ';' || 
                       clean_query[len - 1] == ' ' || 
                       clean_query[len - 1] == '\n' || 
                       clean_query[len - 1] == '\r')) 
    {
        clean_query[len - 1] = '\0';
        len--;
    }

    appendPQExpBuffer(sql, "CREATE TEMPORARY VIEW pg_ozerki_tmp_deps AS SELECT 1 FROM (%s) AS sub", clean_query);
    res = PQexec(conn, sql->data);
    
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        pg_log_error("Failed to exec query for parsing views: %s", PQresultErrorMessage(res));
        PQclear(res);
        destroyPQExpBuffer(sql);
        return;
    }
    PQclear(res);


    const char *dep_query = 
        "SELECT c.oid, c.relname, c.relkind "
        "FROM pg_depend d "
        "JOIN pg_class c ON c.oid = d.refobjid "
        "WHERE d.objid = ("
        "    SELECT oid FROM pg_rewrite WHERE ev_class = 'pg_ozerki_tmp_deps'::regclass"
        ") "
        "AND d.refclassid = 'pg_class'::regclass "
        "AND c.oid != 'pg_ozerki_tmp_deps'::regclass"; 

    res = PQexec(conn, dep_query);
    
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int ntups = PQntuples(res);
        for (int i = 0; i < ntups; i++) {
            Oid relid = atooid(PQgetvalue(res, i, 0));
            char *relname = PQgetvalue(res, i, 1);
            char relkind = PQgetvalue(res, i, 2)[0];


            if (relkind == 'v' || relkind == 'm') { // 'v' - view, 'm' - matview
                add_view_to_deps(deps, relid);
            }
        }
    }
    PQclear(res);

    PQexec(conn, "DROP VIEW pg_ozerki_tmp_deps;");
    destroyPQExpBuffer(sql);

	if (deps->viewCount > 0) {
		pg_log_debug("FOUND VIEWS FOR TABLES");
	}
}

void get_query_from_file(char* filename, PQExpBuffer query) {
	FILE* fp = fopen(filename, "r");
	char buf[1024] = {0};
	if (fp == NULL) {
		pg_log_error("Can't read query from file %s", filename);
		exit(1);
	}

	while(fread(buf, sizeof(char), BATCH_SIZE, fp)) {
		appendPQExpBufferStr(query, buf);
		memset(buf, 0, BATCH_SIZE);
	}

	fclose(fp);
}

int main(int argc, char** argv) {
    int			c;
	const char *filename = NULL;
	
	const char *format = "p";
	TableInfo  *tblinfo;
	int			numTables;
	DumpableObject **dobjs;
	ArchiveMode archiveMode;
	ArchiveFormat archiveFormat;
	int			numObjs;
	char	   *use_role = NULL;
	const char *dumpencoding = NULL;
	const char *dumpsnapshot = NULL;
	pg_compress_specification compression_spec = {0};
	bool dosync = true;
	DumpableObject *boundaryObjs;
	int			i;
	int			optindex;
	DataDirSyncMethod sync_method = DATA_DIR_SYNC_METHOD_FSYNC;
	bool by_query = false;
	char* dump_query = NULL;
	RestoreOptions *ropt;
	Archive    *fout;	
    static DumpOptions dopt;

	bool dump_explain = false;
	char* explain_file = NULL;
	bool dump_explain_analyze = false;
	char* explain_file_analyze = NULL;

	bool dump_stat = false;
	char* stats_file = NULL;

	bool dump_schema = false;


	char* query_filename = NULL;
	bool query_file = false;

	PQExpBuffer query_file_text;

	pg_logging_init(argv[0]);
	pg_logging_set_level(PG_LOG_DEBUG);

	InitDumpOptions(&dopt);
	archiveMode = archModeWrite;
	archiveFormat = archNull;
    
	
	dopt.schemaOnly = true;
	dopt.include_everything = true;

	static struct option long_options[] = {
		

		/*
		 * the following options don't have an equivalent short option letter
		 */
		{"dbname", required_argument, NULL, DBNAME},
		{"username", required_argument, NULL, USERNAME},
		{"host", required_argument, NULL, HOST},
		{"port", required_argument, NULL, PORT},
		{"schema-file", required_argument, NULL, SCHEMA_FILE},
		{"query", required_argument, NULL, QUERY},
		{"explainfile", required_argument, NULL, EXPLAINFILE},
		{"explainfile-analyze", required_argument, NULL, EXPLAINFILE_ANALZYE},
		{"stats-file", required_argument, NULL, STATS_FILE},
		{"no-checks", no_argument, NULL, NO_CHECKS},
		{"no-exts", no_argument, NULL, NO_EXTS},
		{"query-file", required_argument, NULL, QUERY_FILE},
		{NULL, 0, NULL, 0}
	};

	while ((c = getopt_long(argc, argv, "abBcCd:e:E:f:F:h:j:n:N:Op:RsS:t:T:U:vwWxZ:",
							long_options, &optindex)) != -1)
	{
		switch (c)
		{
			case DBNAME:
				dopt.cparams.dbname = pg_strdup(optarg);
				break;
			case USERNAME:
				dopt.cparams.username = pg_strdup(optarg);
				break;
			case HOST:
				dopt.cparams.pghost = pg_strdup(optarg);
				break;
			case PORT:
				dopt.cparams.pgport = pg_strdup(optarg);
				break;
			case SCHEMA_FILE:
				dump_schema = true;
				filename = pg_strdup(optarg);
				break;
			case QUERY:
				by_query = true;
				dump_query = pg_strdup(optarg);
				break;
			case EXPLAINFILE:
				dump_explain = true;
				explain_file = pg_strdup(optarg);
				break;
			case EXPLAINFILE_ANALZYE:
				dump_explain_analyze = true;
				explain_file_analyze = pg_strdup(optarg);
				break;
			case STATS_FILE:
				dump_stat = true;
				stats_file = pg_strdup(optarg);
				break;
			case NO_CHECKS:
				no_checks = true;
				break;
			case NO_EXTS:
				no_exts = true;
				break;
			case QUERY_FILE:
				query_file = true;
				query_filename = pg_strdup(optarg);
				break;
			default:
				/* getopt_long already emitted a complaint */
				pg_log_error_hint("Try \"%s --help\" for more information.", progname);
				exit(1);
		}
	}

	

    if (by_query && query_file) {
		pg_log_error_hint("You can't use --query and --query-file flags both.");
		exit(1);
	}
	fout = CreateArchive(filename, archiveFormat, compression_spec,
						 dosync, archiveMode, setupDumpWorker, sync_method);


	SetArchiveOptions(fout, &dopt, NULL);

	fout->minRemoteVersion = 90200;
	fout->maxRemoteVersion = (PG_VERSION_NUM / 100) * 100 + 99;

	fout->numWorkers = 0;
	/*
	 * Open the database using the Archiver, so it knows about it. Errors mean
	 * death.
	 */

	//use_role = dopt.cparams.username;
	ConnectDatabase(fout, &dopt.cparams, false);
	setup_connection(fout, dumpencoding, dumpsnapshot, use_role);

	pg_log_debug("Connected to database");
	deps = InitQueryDependencies();
	planner_settings = get_planner_settings(fout);
	if (by_query || query_file) {
		deps->been_analyzed = true;
		if (query_file){
			query_file_text = createPQExpBuffer();
			get_query_from_file(query_filename, query_file_text);
			dump_query = pg_strdup(query_file_text->data);

		}
		extract_tables_from_query_text(GetConnection(fout), dump_query, deps);

		find_views_for_tables(GetConnection(fout), deps, dump_query);
		for (int i = 0; i < deps->tableCount; i++) {
			simple_string_list_append(&table_include_patterns, deps->tableNames[i]);
		}

		for (int i = 0; i < deps->schemaCount; i++) {
			simple_string_list_append(&schema_include_patterns, deps->schemas[i]);
		}

		if (deps->tableCount > 0) dopt.include_everything = true;

		expand_schema_name_patterns(fout, &schema_include_patterns,
							   &schema_include_oids,
							   strict_names);

		expand_table_name_patterns(fout, &table_include_patterns,
							   &table_include_oids,
							   strict_names, false);
		
		if (dump_explain) {
			get_explain(GetConnection(fout), dump_query, explain_file, false);
			pg_log_debug("EXPLAIN exported");
		}
		if (dump_explain_analyze) {
			get_explain(GetConnection(fout), dump_query, explain_file_analyze, true);
			pg_log_debug("EXPLAIN ANALYZE exported");
		}

	}

	
	destroyPQExpBuffer(query_file_text);
	
	if (dump_stat) {
		export_stats(GetConnection(fout), stats_file);
		pg_log_debug("Stats exported");
	}

	if (!dump_schema) {
		exit(0);
	}
	collectRoleNames(fout);
	tblinfo = getSchemaData(fout, &numTables, deps);
	getDependencies(fout);

	boundaryObjs = createBoundaryObjects();

	/* Get pointers to all the known DumpableObjects */
	getDumpableObjects(&dobjs, &numObjs);

	/*
	 * Add dummy dependencies to enforce the dump section ordering.
	 */
	addBoundaryDependencies(dobjs, numObjs, boundaryObjs);

	/*
	 * Sort the objects into a safe dump order (no forward references).
	 *
	 * We rely on dependency information to help us determine a safe order, so
	 * the initial sort is mostly for cosmetic purposes: we sort by name to
	 * ensure that logically identical schemas will dump identically.
	 */
	sortDumpableObjectsByTypeName(dobjs, numObjs);
	
	
	sortDumpableObjects(dobjs, numObjs,
						boundaryObjs[0].dumpId, boundaryObjs[1].dumpId);
	    

	for (i = 0; i < numObjs; i++)
		dumpDumpableObject(fout, dobjs[i]);

	
	ropt = NewRestoreOptions();
	ropt->filename = filename;

	/* if you change this list, see dumpOptionsFromRestoreOptions */
	ropt->cparams.dbname = dopt.cparams.dbname ? pg_strdup(dopt.cparams.dbname) : NULL;
	ropt->cparams.pgport = dopt.cparams.pgport ? pg_strdup(dopt.cparams.pgport) : NULL;
	ropt->cparams.pghost = dopt.cparams.pghost ? pg_strdup(dopt.cparams.pghost) : NULL;
	ropt->cparams.username = dopt.cparams.username ? pg_strdup(dopt.cparams.username) : NULL;
	ropt->cparams.promptPassword = dopt.cparams.promptPassword;
	ropt->dropSchema = dopt.outputClean;
	ropt->dataOnly = dopt.dataOnly;
	ropt->schemaOnly = dopt.schemaOnly;
	ropt->if_exists = dopt.if_exists;
	ropt->column_inserts = dopt.column_inserts;
	ropt->dumpSections = dopt.dumpSections;
	ropt->aclsSkip = dopt.aclsSkip;
	ropt->superuser = dopt.outputSuperuser;
	ropt->createDB = dopt.outputCreateDB;
	ropt->noOwner = dopt.outputNoOwner;
	ropt->noTableAm = dopt.outputNoTableAm;
	ropt->noTablespace = dopt.outputNoTablespaces;
	ropt->disable_triggers = dopt.disable_triggers;
	ropt->use_setsessauth = dopt.use_setsessauth;
	ropt->disable_dollar_quoting = dopt.disable_dollar_quoting;
	ropt->dump_inserts = dopt.dump_inserts;
	ropt->no_comments = dopt.no_comments;
	ropt->no_publications = dopt.no_publications;
	ropt->no_security_labels = dopt.no_security_labels;
	ropt->no_subscriptions = dopt.no_subscriptions;
	ropt->lockWaitTimeout = dopt.lockWaitTimeout;
	ropt->include_everything = dopt.include_everything;
	ropt->enable_row_security = dopt.enable_row_security;
	ropt->sequence_data = dopt.sequence_data;
	ropt->binary_upgrade = dopt.binary_upgrade;

	ropt->compression_spec = compression_spec;

	ropt->suppressDumpWarnings = true;	/* We've already shown them */

	SetArchiveOptions(fout, &dopt, ropt);
	ProcessArchiveRestoreOptions(fout);
	

	RestoreArchive(fout);	
	CloseArchive(fout);
    
	pg_log_debug("Schema exported");
    return 0;
}


static void
expand_schema_name_patterns(Archive *fout,
							SimpleStringList *patterns,
							SimpleOidList *oids,
							bool strict_names)
{
	PQExpBuffer query;
	PGresult   *res;
	SimpleStringListCell *cell;
	int			i;

	if (patterns->head == NULL)
		return;					/* nothing to do */

	query = createPQExpBuffer();

	/*
	 * The loop below runs multiple SELECTs might sometimes result in
	 * duplicate entries in the OID list, but we don't care.
	 */

	for (cell = patterns->head; cell; cell = cell->next)
	{
		PQExpBufferData dbbuf;
		int			dotcnt;

		appendPQExpBufferStr(query,
							 "SELECT oid FROM pg_catalog.pg_namespace n\n");
		initPQExpBuffer(&dbbuf);
		processSQLNamePattern(GetConnection(fout), query, cell->val, false,
							  false, NULL, "n.nspname", NULL, NULL, &dbbuf,
							  &dotcnt);
		if (dotcnt > 1)
			pg_fatal("improper qualified name (too many dotted names): %s",
					 cell->val);
		else if (dotcnt == 1)
			prohibit_crossdb_refs(GetConnection(fout), dbbuf.data, cell->val);
		termPQExpBuffer(&dbbuf);

		res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);
		if (strict_names && PQntuples(res) == 0)
			pg_fatal("no matching schemas were found for pattern \"%s\"", cell->val);

		for (i = 0; i < PQntuples(res); i++)
		{
			simple_oid_list_append(oids, atooid(PQgetvalue(res, i, 0)));
		}

		PQclear(res);
		resetPQExpBuffer(query);
	}

	destroyPQExpBuffer(query);
}


TocEntry *
getTocEntryByDumpId(ArchiveHandle *AH, DumpId id)
{
	/* build index arrays if we didn't already */
	if (AH->tocsByDumpId == NULL)
		buildTocEntryArrays(AH);

	if (id > 0 && id <= AH->maxDumpId)
		return AH->tocsByDumpId[id];

	return NULL;
}
static void
buildTocEntryArrays(ArchiveHandle *AH)
{
	DumpId		maxDumpId = AH->maxDumpId;
	TocEntry   *te;

	AH->tocsByDumpId = (TocEntry **) pg_malloc0((maxDumpId + 1) * sizeof(TocEntry *));
	AH->tableDataId = (DumpId *) pg_malloc0((maxDumpId + 1) * sizeof(DumpId));

	for (te = AH->toc->next; te != AH->toc; te = te->next)
	{
		/* this check is purely paranoia, maxDumpId should be correct */
		if (te->dumpId <= 0 || te->dumpId > maxDumpId)
			pg_fatal("bad dumpId");

		/* tocsByDumpId indexes all TOCs by their dump ID */
		AH->tocsByDumpId[te->dumpId] = te;

		/*
		 * tableDataId provides the TABLE DATA item's dump ID for each TABLE
		 * TOC entry that has a DATA item.  We compute this by reversing the
		 * TABLE DATA item's dependency, knowing that a TABLE DATA item has
		 * just one dependency and it is the TABLE item.
		 */
		if (strcmp(te->desc, "TABLE DATA") == 0 && te->nDeps > 0)
		{
			DumpId		tableId = te->dependencies[0];

			/*
			 * The TABLE item might not have been in the archive, if this was
			 * a data-only dump; but its dump ID should be less than its data
			 * item's dump ID, so there should be a place for it in the array.
			 */
			if (tableId <= 0 || tableId > maxDumpId)
				pg_fatal("bad table dumpId for TABLE DATA item");

			AH->tableDataId[tableId] = te->dumpId;
		}
	}
}
void
ProcessArchiveRestoreOptions(Archive *AHX)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;
	RestoreOptions *ropt = AH->public.ropt;
	TocEntry   *te;
	teSection	curSection;

	/* Decide which TOC entries will be dumped/restored, and mark them */
	curSection = SECTION_PRE_DATA;
	for (te = AH->toc->next; te != AH->toc; te = te->next)
	{
		/*
		 * When writing an archive, we also take this opportunity to check
		 * that we have generated the entries in a sane order that respects
		 * the section divisions.  When reading, don't complain, since buggy
		 * old versions of pg_dump might generate out-of-order archives.
		 */
		if (AH->mode != archModeRead)
		{
			switch (te->section)
			{
				case SECTION_NONE:
					/* ok to be anywhere */
					break;
				case SECTION_PRE_DATA:
					if (curSection != SECTION_PRE_DATA)
						pg_log_warning("archive items not in correct section order");
					break;
				case SECTION_DATA:
					if (curSection == SECTION_POST_DATA)
						pg_log_warning("archive items not in correct section order");
					break;
				case SECTION_POST_DATA:
					/* ok no matter which section we were in */
					break;
				default:
					pg_fatal("unexpected section code %d",
							 (int) te->section);
					break;
			}
		}

		if (te->section != SECTION_NONE)
			curSection = te->section;

		te->reqs = _tocEntryRequired(te, curSection, AH);
	}

	/* Enforce strict names checking */
	if (ropt->strict_names)
		StrictNamesCheck(ropt);
}

static bool
_tocEntryIsACL(TocEntry *te)
{
	/* "ACL LANGUAGE" was a crock emitted only in PG 7.4 */
	if (strcmp(te->desc, "ACL") == 0 ||
		strcmp(te->desc, "ACL LANGUAGE") == 0 ||
		strcmp(te->desc, "DEFAULT ACL") == 0)
		return true;
	return false;
}

static int
_tocEntryRequired(TocEntry *te, teSection curSection, ArchiveHandle *AH)
{
	int			res = REQ_SCHEMA | REQ_DATA;
	RestoreOptions *ropt = AH->public.ropt;

	/* These items are treated specially */
	if (strcmp(te->desc, "ENCODING") == 0 ||
		strcmp(te->desc, "STDSTRINGS") == 0 ||
		strcmp(te->desc, "SEARCHPATH") == 0)
		return REQ_SPECIAL;

	/*
	 * DATABASE and DATABASE PROPERTIES also have a special rule: they are
	 * restored in createDB mode, and not restored otherwise, independently of
	 * all else.
	 */
	if (strcmp(te->desc, "DATABASE") == 0 ||
		strcmp(te->desc, "DATABASE PROPERTIES") == 0)
	{
		if (ropt->createDB)
			return REQ_SCHEMA;
		else
			return 0;
	}

	/*
	 * Process exclusions that affect certain classes of TOC entries.
	 */

	/* If it's an ACL, maybe ignore it */
	if (ropt->aclsSkip && _tocEntryIsACL(te))
		return 0;

	/* If it's a comment, maybe ignore it */
	if (ropt->no_comments && strcmp(te->desc, "COMMENT") == 0)
		return 0;

	/*
	 * If it's a publication or a table part of a publication, maybe ignore
	 * it.
	 */
	if (ropt->no_publications &&
		(strcmp(te->desc, "PUBLICATION") == 0 ||
		 strcmp(te->desc, "PUBLICATION TABLE") == 0 ||
		 strcmp(te->desc, "PUBLICATION TABLES IN SCHEMA") == 0))
		return 0;

	/* If it's a security label, maybe ignore it */
	if (ropt->no_security_labels && strcmp(te->desc, "SECURITY LABEL") == 0)
		return 0;

	/* If it's a subscription, maybe ignore it */
	if (ropt->no_subscriptions && strcmp(te->desc, "SUBSCRIPTION") == 0)
		return 0;

	/* Ignore it if section is not to be dumped/restored */
	switch (curSection)
	{
		case SECTION_PRE_DATA:
			if (!(ropt->dumpSections & DUMP_PRE_DATA))
				return 0;
			break;
		case SECTION_DATA:
			if (!(ropt->dumpSections & DUMP_DATA))
				return 0;
			break;
		case SECTION_POST_DATA:
			if (!(ropt->dumpSections & DUMP_POST_DATA))
				return 0;
			break;
		default:
			/* shouldn't get here, really, but ignore it */
			return 0;
	}

	/* Ignore it if rejected by idWanted[] (cf. SortTocFromFile) */
	if (ropt->idWanted && !ropt->idWanted[te->dumpId - 1])
		return 0;

	/*
	 * Check options for selective dump/restore.
	 */
	if (strcmp(te->desc, "ACL") == 0 ||
		strcmp(te->desc, "COMMENT") == 0 ||
		strcmp(te->desc, "SECURITY LABEL") == 0)
	{
		/* Database properties react to createDB, not selectivity options. */
		if (strncmp(te->tag, "DATABASE ", 9) == 0)
		{
			if (!ropt->createDB)
				return 0;
		}
		else if (ropt->schemaNames.head != NULL ||
				 ropt->schemaExcludeNames.head != NULL ||
				 ropt->selTypes)
		{
			/*
			 * In a selective dump/restore, we want to restore these dependent
			 * TOC entry types only if their parent object is being restored.
			 * Without selectivity options, we let through everything in the
			 * archive.  Note there may be such entries with no parent, eg
			 * non-default ACLs for built-in objects.  Also, we make
			 * per-column ACLs additionally depend on the table's ACL if any
			 * to ensure correct restore order, so those dependencies should
			 * be ignored in this check.
			 *
			 * This code depends on the parent having been marked already,
			 * which should be the case; if it isn't, perhaps due to
			 * SortTocFromFile rearrangement, skipping the dependent entry
			 * seems prudent anyway.
			 *
			 * Ideally we'd handle, eg, table CHECK constraints this way too.
			 * But it's hard to tell which of their dependencies is the one to
			 * consult.
			 */
			bool		dumpthis = false;

			for (int i = 0; i < te->nDeps; i++)
			{
				TocEntry   *pte = getTocEntryByDumpId(AH, te->dependencies[i]);

				if (!pte)
					continue;	/* probably shouldn't happen */
				if (strcmp(pte->desc, "ACL") == 0)
					continue;	/* ignore dependency on another ACL */
				if (pte->reqs == 0)
					continue;	/* this object isn't marked, so ignore it */
				/* Found a parent to be dumped, so we want to dump this too */
				dumpthis = true;
				break;
			}
			if (!dumpthis)
				return 0;
		}
	}
	else
	{
		/* Apply selective-restore rules for standalone TOC entries. */
		if (ropt->schemaNames.head != NULL)
		{
			/* If no namespace is specified, it means all. */
			if (!te->namespace)
				return 0;
			if (!simple_string_list_member(&ropt->schemaNames, te->namespace))
				return 0;
		}

		if (ropt->schemaExcludeNames.head != NULL &&
			te->namespace &&
			simple_string_list_member(&ropt->schemaExcludeNames, te->namespace))
			return 0;

		if (ropt->selTypes)
		{
			if (strcmp(te->desc, "TABLE") == 0 ||
				strcmp(te->desc, "TABLE DATA") == 0 ||
				strcmp(te->desc, "VIEW") == 0 ||
				strcmp(te->desc, "FOREIGN TABLE") == 0 ||
				strcmp(te->desc, "MATERIALIZED VIEW") == 0 ||
				strcmp(te->desc, "MATERIALIZED VIEW DATA") == 0 ||
				strcmp(te->desc, "SEQUENCE") == 0 ||
				strcmp(te->desc, "SEQUENCE SET") == 0)
			{
				if (!ropt->selTable)
					return 0;
				if (ropt->tableNames.head != NULL &&
					!simple_string_list_member(&ropt->tableNames, te->tag))
					return 0;
			}
			else if (strcmp(te->desc, "INDEX") == 0)
			{
				if (!ropt->selIndex)
					return 0;
				if (ropt->indexNames.head != NULL &&
					!simple_string_list_member(&ropt->indexNames, te->tag))
					return 0;
			}
			else if (strcmp(te->desc, "FUNCTION") == 0 ||
					 strcmp(te->desc, "AGGREGATE") == 0 ||
					 strcmp(te->desc, "PROCEDURE") == 0)
			{
				if (!ropt->selFunction)
					return 0;
				if (ropt->functionNames.head != NULL &&
					!simple_string_list_member(&ropt->functionNames, te->tag))
					return 0;
			}
			else if (strcmp(te->desc, "TRIGGER") == 0)
			{
				if (!ropt->selTrigger)
					return 0;
				if (ropt->triggerNames.head != NULL &&
					!simple_string_list_member(&ropt->triggerNames, te->tag))
					return 0;
			}
			else
				return 0;
		}
	}

	/*
	 * Determine whether the TOC entry contains schema and/or data components,
	 * and mask off inapplicable REQ bits.  If it had a dataDumper, assume
	 * it's both schema and data.  Otherwise it's probably schema-only, but
	 * there are exceptions.
	 */
	if (!te->hadDumper)
	{
		/*
		 * Special Case: If 'SEQUENCE SET' or anything to do with LOs, then it
		 * is considered a data entry.  We don't need to check for BLOBS or
		 * old-style BLOB COMMENTS entries, because they will have hadDumper =
		 * true ... but we do need to check new-style BLOB ACLs, comments,
		 * etc.
		 */
		if (strcmp(te->desc, "SEQUENCE SET") == 0 ||
			strcmp(te->desc, "BLOB") == 0 ||
			strcmp(te->desc, "BLOB METADATA") == 0 ||
			(strcmp(te->desc, "ACL") == 0 &&
			 strncmp(te->tag, "LARGE OBJECT", 12) == 0) ||
			(strcmp(te->desc, "COMMENT") == 0 &&
			 strncmp(te->tag, "LARGE OBJECT", 12) == 0) ||
			(strcmp(te->desc, "SECURITY LABEL") == 0 &&
			 strncmp(te->tag, "LARGE OBJECT", 12) == 0))
			res = res & REQ_DATA;
		else
			res = res & ~REQ_DATA;
	}

	/*
	 * If there's no definition command, there's no schema component.  Treat
	 * "load via partition root" comments as not schema.
	 */
	if (!te->defn || !te->defn[0] ||
		strncmp(te->defn, "-- load via partition root ", 27) == 0)
		res = res & ~REQ_SCHEMA;

	/*
	 * Special case: <Init> type with <Max OID> tag; this is obsolete and we
	 * always ignore it.
	 */
	if ((strcmp(te->desc, "<Init>") == 0) && (strcmp(te->tag, "Max OID") == 0))
		return 0;

	/* Mask it if we only want schema */
	if (ropt->schemaOnly)
	{
		/*
		 * The sequence_data option overrides schemaOnly for SEQUENCE SET.
		 *
		 * In binary-upgrade mode, even with schemaOnly set, we do not mask
		 * out large objects.  (Only large object definitions, comments and
		 * other metadata should be generated in binary-upgrade mode, not the
		 * actual data, but that need not concern us here.)
		 */
		if (!(ropt->sequence_data && strcmp(te->desc, "SEQUENCE SET") == 0) &&
			!(ropt->binary_upgrade &&
			  (strcmp(te->desc, "BLOB") == 0 ||
			   strcmp(te->desc, "BLOB METADATA") == 0 ||
			   (strcmp(te->desc, "ACL") == 0 &&
				strncmp(te->tag, "LARGE OBJECT", 12) == 0) ||
			   (strcmp(te->desc, "COMMENT") == 0 &&
				strncmp(te->tag, "LARGE OBJECT", 12) == 0) ||
			   (strcmp(te->desc, "SECURITY LABEL") == 0 &&
				strncmp(te->tag, "LARGE OBJECT", 12) == 0))))
			res = res & REQ_SCHEMA;
	}

	/* Mask it if we only want data */
	if (ropt->dataOnly)
		res = res & REQ_DATA;

	return res;
}

static void
StrictNamesCheck(RestoreOptions *ropt)
{
	const char *missing_name;

	Assert(ropt->strict_names);

	if (ropt->schemaNames.head != NULL)
	{
		missing_name = simple_string_list_not_touched(&ropt->schemaNames);
		if (missing_name != NULL)
			pg_fatal("schema \"%s\" not found", missing_name);
	}

	if (ropt->tableNames.head != NULL)
	{
		missing_name = simple_string_list_not_touched(&ropt->tableNames);
		if (missing_name != NULL)
			pg_fatal("table \"%s\" not found", missing_name);
	}

	if (ropt->indexNames.head != NULL)
	{
		missing_name = simple_string_list_not_touched(&ropt->indexNames);
		if (missing_name != NULL)
			pg_fatal("index \"%s\" not found", missing_name);
	}

	if (ropt->functionNames.head != NULL)
	{
		missing_name = simple_string_list_not_touched(&ropt->functionNames);
		if (missing_name != NULL)
			pg_fatal("function \"%s\" not found", missing_name);
	}

	if (ropt->triggerNames.head != NULL)
	{
		missing_name = simple_string_list_not_touched(&ropt->triggerNames);
		if (missing_name != NULL)
			pg_fatal("trigger \"%s\" not found", missing_name);
	}
}

static void
addBoundaryDependencies(DumpableObject **dobjs, int numObjs,
						DumpableObject *boundaryObjs)
{
	DumpableObject *preDataBound = boundaryObjs + 0;
	DumpableObject *postDataBound = boundaryObjs + 1;
	int			i;

	for (i = 0; i < numObjs; i++)
	{
		DumpableObject *dobj = dobjs[i];

		/*
		 * The classification of object types here must match the SECTION_xxx
		 * values assigned during subsequent ArchiveEntry calls!
		 */
		switch (dobj->objType)
		{
			case DO_NAMESPACE:
			case DO_EXTENSION:
			case DO_TYPE:
			case DO_SHELL_TYPE:
			case DO_FUNC:
			case DO_AGG:
			case DO_OPERATOR:
			case DO_ACCESS_METHOD:
			case DO_OPCLASS:
			case DO_OPFAMILY:
			case DO_COLLATION:
			case DO_CONVERSION:
			case DO_TABLE:
			case DO_TABLE_ATTACH:
			case DO_ATTRDEF:
			case DO_PROCLANG:
			case DO_CAST:
			case DO_DUMMY_TYPE:
			case DO_TSPARSER:
			case DO_TSDICT:
			case DO_TSTEMPLATE:
			case DO_TSCONFIG:
			case DO_FDW:
			case DO_FOREIGN_SERVER:
			case DO_TRANSFORM:
				/* Pre-data objects: must come before the pre-data boundary */
				addObjectDependency(preDataBound, dobj->dumpId);
				break;
			case DO_TABLE_DATA:
			case DO_SEQUENCE_SET:
			case DO_LARGE_OBJECT:
			case DO_LARGE_OBJECT_DATA:
				/* Data objects: must come between the boundaries */
				addObjectDependency(dobj, preDataBound->dumpId);
				addObjectDependency(postDataBound, dobj->dumpId);
				break;
			case DO_INDEX:
			case DO_INDEX_ATTACH:
			case DO_STATSEXT:
			case DO_REFRESH_MATVIEW:
			case DO_TRIGGER:
			case DO_EVENT_TRIGGER:
			case DO_DEFAULT_ACL:
			case DO_POLICY:
			case DO_PUBLICATION:
			case DO_PUBLICATION_REL:
			case DO_PUBLICATION_TABLE_IN_SCHEMA:
			case DO_SUBSCRIPTION:
			case DO_SUBSCRIPTION_REL:
				/* Post-data objects: must come after the post-data boundary */
				addObjectDependency(dobj, postDataBound->dumpId);
				break;
			case DO_RULE:
				/* Rules are post-data, but only if dumped separately */
				if (((RuleInfo *) dobj)->separate)
					addObjectDependency(dobj, postDataBound->dumpId);
				break;
			case DO_CONSTRAINT:
			case DO_FK_CONSTRAINT:
				/* Constraints are post-data, but only if dumped separately */
				if (((ConstraintInfo *) dobj)->separate)
					addObjectDependency(dobj, postDataBound->dumpId);
				break;
			case DO_PRE_DATA_BOUNDARY:
				/* nothing to do */
				break;
			case DO_POST_DATA_BOUNDARY:
				/* must come after the pre-data boundary */
				addObjectDependency(dobj, preDataBound->dumpId);
				break;
		}
	}
}


static void
getDependencies(Archive *fout)
{
	PQExpBuffer query;
	PGresult   *res;
	int			ntups,
				i;
	int			i_classid,
				i_objid,
				i_refclassid,
				i_refobjid,
				i_deptype;
	DumpableObject *dobj,
			   *refdobj;

	pg_log_info("reading dependency data");

	query = createPQExpBuffer();

	/*
	 * Messy query to collect the dependency data we need.  Note that we
	 * ignore the sub-object column, so that dependencies of or on a column
	 * look the same as dependencies of or on a whole table.
	 *
	 * PIN dependencies aren't interesting, and EXTENSION dependencies were
	 * already processed by getExtensionMembership.
	 */
	appendPQExpBufferStr(query, "SELECT "
						 "classid, objid, refclassid, refobjid, deptype "
						 "FROM pg_depend "
						 "WHERE deptype != 'p' AND deptype != 'e'\n");

	/*
	 * Since we don't treat pg_amop entries as separate DumpableObjects, we
	 * have to translate their dependencies into dependencies of their parent
	 * opfamily.  Ignore internal dependencies though, as those will point to
	 * their parent opclass, which we needn't consider here (and if we did,
	 * it'd just result in circular dependencies).  Also, "loose" opfamily
	 * entries will have dependencies on their parent opfamily, which we
	 * should drop since they'd likewise become useless self-dependencies.
	 * (But be sure to keep deps on *other* opfamilies; see amopsortfamily.)
	 */
	appendPQExpBufferStr(query, "UNION ALL\n"
						 "SELECT 'pg_opfamily'::regclass AS classid, amopfamily AS objid, refclassid, refobjid, deptype "
						 "FROM pg_depend d, pg_amop o "
						 "WHERE deptype NOT IN ('p', 'e', 'i') AND "
						 "classid = 'pg_amop'::regclass AND objid = o.oid "
						 "AND NOT (refclassid = 'pg_opfamily'::regclass AND amopfamily = refobjid)\n");

	/* Likewise for pg_amproc entries */
	appendPQExpBufferStr(query, "UNION ALL\n"
						 "SELECT 'pg_opfamily'::regclass AS classid, amprocfamily AS objid, refclassid, refobjid, deptype "
						 "FROM pg_depend d, pg_amproc p "
						 "WHERE deptype NOT IN ('p', 'e', 'i') AND "
						 "classid = 'pg_amproc'::regclass AND objid = p.oid "
						 "AND NOT (refclassid = 'pg_opfamily'::regclass AND amprocfamily = refobjid)\n");

	/* Sort the output for efficiency below */
	appendPQExpBufferStr(query, "ORDER BY 1,2");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_classid = PQfnumber(res, "classid");
	i_objid = PQfnumber(res, "objid");
	i_refclassid = PQfnumber(res, "refclassid");
	i_refobjid = PQfnumber(res, "refobjid");
	i_deptype = PQfnumber(res, "deptype");

	/*
	 * Since we ordered the SELECT by referencing ID, we can expect that
	 * multiple entries for the same object will appear together; this saves
	 * on searches.
	 */
	dobj = NULL;

	for (i = 0; i < ntups; i++)
	{
		CatalogId	objId;
		CatalogId	refobjId;
		char		deptype;

		objId.tableoid = atooid(PQgetvalue(res, i, i_classid));
		objId.oid = atooid(PQgetvalue(res, i, i_objid));
		refobjId.tableoid = atooid(PQgetvalue(res, i, i_refclassid));
		refobjId.oid = atooid(PQgetvalue(res, i, i_refobjid));
		deptype = *(PQgetvalue(res, i, i_deptype));

		if (dobj == NULL ||
			dobj->catId.tableoid != objId.tableoid ||
			dobj->catId.oid != objId.oid)
			dobj = findObjectByCatalogId(objId);

		/*
		 * Failure to find objects mentioned in pg_depend is not unexpected,
		 * since for example we don't collect info about TOAST tables.
		 */
		if (dobj == NULL)
		{
#ifdef NOT_USED
			pg_log_warning("no referencing object %u %u",
						   objId.tableoid, objId.oid);
#endif
			continue;
		}

		refdobj = findObjectByCatalogId(refobjId);

		if (refdobj == NULL)
		{
#ifdef NOT_USED
			pg_log_warning("no referenced object %u %u",
						   refobjId.tableoid, refobjId.oid);
#endif
			continue;
		}

		/*
		 * For 'x' dependencies, mark the object for later; we still add the
		 * normal dependency, for possible ordering purposes.  Currently
		 * pg_dump_sort.c knows to put extensions ahead of all object types
		 * that could possibly depend on them, but this is safer.
		 */
		if (deptype == 'x')
			dobj->depends_on_ext = true;

		/*
		 * Ordinarily, table rowtypes have implicit dependencies on their
		 * tables.  However, for a composite type the implicit dependency goes
		 * the other way in pg_depend; which is the right thing for DROP but
		 * it doesn't produce the dependency ordering we need. So in that one
		 * case, we reverse the direction of the dependency.
		 */
		if (deptype == 'i' &&
			dobj->objType == DO_TABLE &&
			refdobj->objType == DO_TYPE)
			addObjectDependency(refdobj, dobj->dumpId);
		else
			/* normal case */
			addObjectDependency(dobj, refdobj->dumpId);
	}

	PQclear(res);

	destroyPQExpBuffer(query);
}


bool is_in_view_oids(Oid oid) {
	for (int i = 0; i < deps->viewCount; i++) {
		if (oid == deps->viewOids[i]){ 
			return true;
		}
	}
	return false;
}


static void
setup_connection(Archive *AH, const char *dumpencoding,
				 const char *dumpsnapshot, char *use_role)
{
	DumpOptions *dopt = AH->dopt;
	PGconn	   *conn = GetConnection(AH);
	const char *std_strings;

	
	//PQclear(ExecuteSqlQueryForSingleRow(AH, ALWAYS_SECURE_SEARCH_PATH_SQL));
	/*
	 * Set the client encoding if requested.
	 */
	if (dumpencoding)
	{
		if (PQsetClientEncoding(conn, dumpencoding) < 0)
			pg_fatal("invalid client encoding \"%s\" specified",
					 dumpencoding);
	}

	/*
	 * Get the active encoding and the standard_conforming_strings setting, so
	 * we know how to escape strings.
	 */
	
	AH->encoding = PQclientEncoding(conn);
	
	setFmtEncoding(AH->encoding);

	std_strings = PQparameterStatus(conn, "standard_conforming_strings");
	AH->std_strings = (std_strings && strcmp(std_strings, "on") == 0);
	
	/*
	 * Set the role if requested.  In a parallel dump worker, we'll be passed
	 * use_role == NULL, but AH->use_role is already set (if user specified it
	 * originally) and we should use that.
	 */
	if (!use_role && AH->use_role)
		use_role = AH->use_role;

	/* Set the role if requested */
	if (use_role)
	{
		PQExpBuffer query = createPQExpBuffer();

		appendPQExpBuffer(query, "SET ROLE %s", fmtId(use_role));
		ExecuteSqlStatement(AH, query->data);
		destroyPQExpBuffer(query);

		/* save it for possible later use by parallel workers */
		if (!AH->use_role)
			AH->use_role = pg_strdup(use_role);
	}

	PGresult* search_path_res = ExecuteSqlQueryForSingleRow(AH, "SHOW search_path");
	
	pg_log_debug("current search_path %s", PQgetvalue(search_path_res, 0, 0));
	/* Set the datestyle to ISO to ensure the dump's portability */
	ExecuteSqlStatement(AH, "SET DATESTYLE = ISO");

	/* Likewise, avoid using sql_standard intervalstyle */
	ExecuteSqlStatement(AH, "SET INTERVALSTYLE = POSTGRES");

	/*
	 * Use an explicitly specified extra_float_digits if it has been provided.
	 * Otherwise, set extra_float_digits so that we can dump float data
	 * exactly (given correctly implemented float I/O code, anyway).
	 */
	if (have_extra_float_digits)
	{
		PQExpBuffer q = createPQExpBuffer();

		appendPQExpBuffer(q, "SET extra_float_digits TO %d",
						  extra_float_digits);
		ExecuteSqlStatement(AH, q->data);
		destroyPQExpBuffer(q);
	}
	else
		ExecuteSqlStatement(AH, "SET extra_float_digits TO 3");

	/*
	 * Disable synchronized scanning, to prevent unpredictable changes in row
	 * ordering across a dump and reload.
	 */
	ExecuteSqlStatement(AH, "SET synchronize_seqscans TO off");

	/*
	 * Disable timeouts if supported.
	 */
	ExecuteSqlStatement(AH, "SET statement_timeout = 0");
	if (AH->remoteVersion >= 90300)
		ExecuteSqlStatement(AH, "SET lock_timeout = 0");
	if (AH->remoteVersion >= 90600)
		ExecuteSqlStatement(AH, "SET idle_in_transaction_session_timeout = 0");
	if (AH->remoteVersion >= 170000)
		ExecuteSqlStatement(AH, "SET transaction_timeout = 0");

	/*
	 * Quote all identifiers, if requested.
	 */
	if (quote_all_identifiers)
		ExecuteSqlStatement(AH, "SET quote_all_identifiers = true");

	/*
	 * Adjust row-security mode, if supported.
	 */
	if (AH->remoteVersion >= 90500)
	{
		if (dopt->enable_row_security)
			ExecuteSqlStatement(AH, "SET row_security = on");
		else
			ExecuteSqlStatement(AH, "SET row_security = off");
	}

	/*
	 * For security reasons, we restrict the expansion of non-system views and
	 * access to foreign tables during the pg_dump process. This restriction
	 * is adjusted when dumping foreign table data.
	 */
	set_restrict_relation_kind(AH, "foreign-table");

	/*
	 * Initialize prepared-query state to "nothing prepared".  We do this here
	 * so that a parallel dump worker will have its own state.
	 */
	AH->is_prepared = (bool *) pg_malloc0(NUM_PREP_QUERIES * sizeof(bool));

	/*
	 * Start transaction-snapshot mode transaction to dump consistent data.
	 */
	ExecuteSqlStatement(AH, "BEGIN");

	/*
	 * To support the combination of serializable_deferrable with the jobs
	 * option we use REPEATABLE READ for the worker connections that are
	 * passed a snapshot.  As long as the snapshot is acquired in a
	 * SERIALIZABLE, READ ONLY, DEFERRABLE transaction, its use within a
	 * REPEATABLE READ transaction provides the appropriate integrity
	 * guarantees.  This is a kluge, but safe for back-patching.
	 */
	if (dopt->serializable_deferrable && AH->sync_snapshot_id == NULL)
		ExecuteSqlStatement(AH,
							"SET TRANSACTION ISOLATION LEVEL "
							"SERIALIZABLE, READ ONLY, DEFERRABLE");
	else
		ExecuteSqlStatement(AH,
							"SET TRANSACTION ISOLATION LEVEL "
							"REPEATABLE READ, READ WRITE");

	/*
	 * If user specified a snapshot to use, select that.  In a parallel dump
	 * worker, we'll be passed dumpsnapshot == NULL, but AH->sync_snapshot_id
	 * is already set (if the server can handle it) and we should use that.
	 */
	if (dumpsnapshot)
		AH->sync_snapshot_id = pg_strdup(dumpsnapshot);

	if (AH->sync_snapshot_id)
	{
		PQExpBuffer query = createPQExpBuffer();

		appendPQExpBufferStr(query, "SET TRANSACTION SNAPSHOT ");
		appendStringLiteralConn(query, AH->sync_snapshot_id, conn);
		ExecuteSqlStatement(AH, query->data);
		destroyPQExpBuffer(query);
	}
}


static void
setupDumpWorker(Archive *AH)
{
	/*
	 * We want to re-select all the same values the leader connection is
	 * using.  We'll have inherited directly-usable values in
	 * AH->sync_snapshot_id and AH->use_role, but we need to translate the
	 * inherited encoding value back to a string to pass to setup_connection.
	 */
	setup_connection(AH,
					 pg_encoding_to_char(AH->encoding),
					 NULL,
					 NULL);
}

static void
set_restrict_relation_kind(Archive *AH, const char *value)
{
	PQExpBuffer query = createPQExpBuffer();
	PGresult   *res;

	appendPQExpBuffer(query,
					  "SELECT set_config(name, '%s', false) "
					  "FROM pg_settings "
					  "WHERE name = 'restrict_nonsystem_relation_kind'",
					  value);
	res = ExecuteSqlQuery(AH, query->data, PGRES_TUPLES_OK);

	PQclear(res);
	destroyPQExpBuffer(query);
}

static void
SetOutput(ArchiveHandle *AH, const char *filename,
		  const pg_compress_specification compression_spec)
{
	CompressFileHandle *CFH;
	const char *mode;
	int			fn = -1;

	if (filename)
	{
		if (strcmp(filename, "-") == 0)
			fn = fileno(stdout);
	}
	else if (AH->FH)
		fn = fileno(AH->FH);
	else if (AH->fSpec)
	{
		filename = AH->fSpec;
	}
	else
		fn = fileno(stdout);

	if (AH->mode == archModeAppend)
		mode = PG_BINARY_A;
	else
		mode = PG_BINARY_W;

	CFH = pg_malloc0(sizeof(CompressFileHandle));
	InitCompressFileHandleNone(CFH, compression_spec);

	if (!CFH->open_func(filename, fn, mode, CFH))
	{
		if (filename)
			pg_fatal("could not open output file \"%s\": %m", filename);
		else
			pg_fatal("could not open output file: %m");
	}

	AH->OF = CFH;
}

static CompressFileHandle *
SaveOutput(ArchiveHandle *AH)
{
	return (CompressFileHandle *) AH->OF;
}

static void
dumpTimestamp(ArchiveHandle *AH, const char *msg, time_t tim)
{
	char		buf[64];

	if (strftime(buf, sizeof(buf), PGDUMP_STRFTIME_FMT, localtime(&tim)) != 0)
		ahprintf(AH, "-- %s %s\n\n", msg, buf);
}

void
StartTransaction(Archive *AHX)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;

	ExecuteSqlCommand(AH, "BEGIN", "could not start database transaction");
}

void
CommitTransaction(Archive *AHX)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;

	ExecuteSqlCommand(AH, "COMMIT", "could not commit database transaction");
}

/*
 * Issue per-blob commands for the large object(s) listed in the TocEntry
 *
 * The TocEntry's defn string is assumed to consist of large object OIDs,
 * one per line.  Wrap these in the given SQL command fragments and issue
 * the commands.  (cmdEnd need not include a semicolon.)
 */
void
IssueCommandPerBlob(ArchiveHandle *AH, TocEntry *te,
					const char *cmdBegin, const char *cmdEnd)
{
	/* Make a writable copy of the command string */
	char	   *buf = pg_strdup(te->defn);
	RestoreOptions *ropt = AH->public.ropt;
	char	   *st;
	char	   *en;

	st = buf;
	while ((en = strchr(st, '\n')) != NULL)
	{
		*en++ = '\0';
		ahprintf(AH, "%s%s%s;\n", cmdBegin, st, cmdEnd);

		/* In --transaction-size mode, count each command as an action */
		if (ropt && ropt->txn_size > 0)
		{
			if (++AH->txnCount >= ropt->txn_size)
			{
				if (AH->connection)
				{
					CommitTransaction(&AH->public);
					StartTransaction(&AH->public);
				}
				else
					ahprintf(AH, "COMMIT;\nBEGIN;\n\n");
				AH->txnCount = 0;
			}
		}

		st = en;
	}
	ahprintf(AH, "\n");
	pg_free(buf);
}

/*
 * Process a "LARGE OBJECTS" ACL TocEntry.
 *
 * To save space in the dump file, the TocEntry contains only one copy
 * of the required GRANT/REVOKE commands, written to apply to the first
 * blob in the group (although we do not depend on that detail here).
 * We must expand the text to generate commands for all the blobs listed
 * in the associated BLOB METADATA entry.
 */
void
IssueACLPerBlob(ArchiveHandle *AH, TocEntry *te)
{
	TocEntry   *blobte = getTocEntryByDumpId(AH, te->dependencies[0]);
	char	   *buf;
	char	   *st;
	char	   *st2;
	char	   *en;
	bool		inquotes;

	if (!blobte)
		pg_fatal("could not find entry for ID %d", te->dependencies[0]);
	Assert(strcmp(blobte->desc, "BLOB METADATA") == 0);

	/* Make a writable copy of the ACL commands string */
	buf = pg_strdup(te->defn);

	/*
	 * We have to parse out the commands sufficiently to locate the blob OIDs
	 * and find the command-ending semicolons.  The commands should not
	 * contain anything hard to parse except for double-quoted role names,
	 * which are easy to ignore.  Once we've split apart the first and second
	 * halves of a command, apply IssueCommandPerBlob.  (This means the
	 * updates on the blobs are interleaved if there's multiple commands, but
	 * that should cause no trouble.)
	 */
	inquotes = false;
	st = en = buf;
	st2 = NULL;
	while (*en)
	{
		/* Ignore double-quoted material */
		if (*en == '"')
			inquotes = !inquotes;
		if (inquotes)
		{
			en++;
			continue;
		}
		/* If we found "LARGE OBJECT", that's the end of the first half */
		if (strncmp(en, "LARGE OBJECT ", 13) == 0)
		{
			/* Terminate the first-half string */
			en += 13;
			Assert(isdigit((unsigned char) *en));
			*en++ = '\0';
			/* Skip the rest of the blob OID */
			while (isdigit((unsigned char) *en))
				en++;
			/* Second half starts here */
			Assert(st2 == NULL);
			st2 = en;
		}
		/* If we found semicolon, that's the end of the second half */
		else if (*en == ';')
		{
			/* Terminate the second-half string */
			*en++ = '\0';
			Assert(st2 != NULL);
			/* Issue this command for each blob */
			IssueCommandPerBlob(AH, blobte, st, st2);
			/* For neatness, skip whitespace before the next command */
			while (isspace((unsigned char) *en))
				en++;
			/* Reset for new command */
			st = en;
			st2 = NULL;
		}
		else
			en++;
	}
	pg_free(buf);
}

void
DropLOIfExists(ArchiveHandle *AH, Oid oid)
{
	ahprintf(AH,
			 "SELECT pg_catalog.lo_unlink(oid) "
			 "FROM pg_catalog.pg_largeobject_metadata "
			 "WHERE oid = '%u';\n",
			 oid);
}




static void
_doSetFixedOutputState(ArchiveHandle *AH)
{
	RestoreOptions *ropt = AH->public.ropt;

	/*
	 * Disable timeouts to allow for slow commands, idle parallel workers, etc
	 */
	ahprintf(AH, "SET statement_timeout = 0;\n");
	ahprintf(AH, "SET lock_timeout = 0;\n");
	ahprintf(AH, "SET idle_in_transaction_session_timeout = 0;\n");
	ahprintf(AH, "SET transaction_timeout = 0;\n");

	/* Select the correct character set encoding */
	ahprintf(AH, "SET client_encoding = '%s';\n",
			 pg_encoding_to_char(AH->public.encoding));

	/* Select the correct string literal syntax */
	ahprintf(AH, "SET standard_conforming_strings = %s;\n",
			 AH->public.std_strings ? "on" : "off");

	/* Select the role to be used during restore */
	if (ropt && ropt->use_role)
		ahprintf(AH, "SET ROLE %s;\n", fmtId(ropt->use_role));

	/* Select the dump-time search_path */
	if (AH->public.searchpath)
		ahprintf(AH, "%s", AH->public.searchpath);

	/* Make sure function checking is disabled */
	ahprintf(AH, "SET check_function_bodies = false;\n");

	/* Ensure that all valid XML data will be accepted */
	ahprintf(AH, "SET xmloption = content;\n");
	ahprintf(AH, planner_settings);
	/* Avoid annoying notices etc */
	ahprintf(AH, "SET client_min_messages = warning;\n");
	if (!AH->public.std_strings)
		ahprintf(AH, "SET escape_string_warning = off;\n");

	/* Adjust row-security state */
	if (ropt && ropt->enable_row_security)
		ahprintf(AH, "SET row_security = on;\n");
	else
		ahprintf(AH, "SET row_security = off;\n");

	/*
	 * In --transaction-size mode, we should always be in a transaction when
	 * we begin to restore objects.
	 */
	if (ropt && ropt->txn_size > 0)
	{
		if (AH->connection)
			StartTransaction(&AH->public);
		else
			ahprintf(AH, "\nBEGIN;\n");
		AH->txnCount = 0;
	}

	ahprintf(AH, "\n");
}


static int
RestoringToDB(ArchiveHandle *AH)
{
	RestoreOptions *ropt = AH->public.ropt;

	return (ropt && ropt->useDB && AH->connection);
}
static void
_selectOutputSchema(ArchiveHandle *AH, const char *schemaName)
{
	PQExpBuffer qry;

	/*
	 * If there was a SEARCHPATH TOC entry, we're supposed to just stay with
	 * that search_path rather than switching to entry-specific paths.
	 * Otherwise, it's an old archive that will not restore correctly unless
	 * we set the search_path as it's expecting.
	 */
	if (AH->public.searchpath)
		return;

	if (!schemaName || *schemaName == '\0' ||
		(AH->currSchema && strcmp(AH->currSchema, schemaName) == 0))
		return;					/* no need to do anything */

	qry = createPQExpBuffer();

	appendPQExpBuffer(qry, "SET search_path = %s",
					  fmtId(schemaName));
	if (strcmp(schemaName, "pg_catalog") != 0)
		appendPQExpBufferStr(qry, ", pg_catalog");

	if (RestoringToDB(AH))
	{
		PGresult   *res;

		res = PQexec(AH->connection, qry->data);

		if (!res || PQresultStatus(res) != PGRES_COMMAND_OK)
			warn_or_exit_horribly(AH,
								  "could not set \"search_path\" to \"%s\": %s",
								  schemaName, PQerrorMessage(AH->connection));

		PQclear(res);
	}
	else
		ahprintf(AH, "%s;\n\n", qry->data);

	free(AH->currSchema);
	AH->currSchema = pg_strdup(schemaName);

	destroyPQExpBuffer(qry);
}



static RestorePass
_tocEntryRestorePass(TocEntry *te)
{
	/* "ACL LANGUAGE" was a crock emitted only in PG 7.4 */
	if (strcmp(te->desc, "ACL") == 0 ||
		strcmp(te->desc, "ACL LANGUAGE") == 0 ||
		strcmp(te->desc, "DEFAULT ACL") == 0)
		return RESTORE_PASS_ACL;
	if (strcmp(te->desc, "EVENT TRIGGER") == 0 ||
		strcmp(te->desc, "MATERIALIZED VIEW DATA") == 0)
		return RESTORE_PASS_POST_ACL;

	/*
	 * Comments need to be emitted in the same pass as their parent objects.
	 * ACLs haven't got comments, and neither do matview data objects, but
	 * event triggers do.  (Fortunately, event triggers haven't got ACLs, or
	 * we'd need yet another weird special case.)
	 */
	if (strcmp(te->desc, "COMMENT") == 0 &&
		strncmp(te->tag, "EVENT TRIGGER ", 14) == 0)
		return RESTORE_PASS_POST_ACL;

	/* All else can be handled in the main pass. */
	return RESTORE_PASS_MAIN;
}

static void
_selectTablespace(ArchiveHandle *AH, const char *tablespace)
{
	RestoreOptions *ropt = AH->public.ropt;
	PQExpBuffer qry;
	const char *want,
			   *have;

	/* do nothing in --no-tablespaces mode */
	if (ropt->noTablespace)
		return;

	have = AH->currTablespace;
	want = tablespace;

	/* no need to do anything for non-tablespace object */
	if (!want)
		return;

	if (have && strcmp(want, have) == 0)
		return;					/* no need to do anything */

	qry = createPQExpBuffer();

	if (strcmp(want, "") == 0)
	{
		/* We want the tablespace to be the database's default */
		appendPQExpBufferStr(qry, "SET default_tablespace = ''");
	}
	else
	{
		/* We want an explicit tablespace */
		appendPQExpBuffer(qry, "SET default_tablespace = %s", fmtId(want));
	}

	if (RestoringToDB(AH))
	{
		PGresult   *res;

		res = PQexec(AH->connection, qry->data);

		if (!res || PQresultStatus(res) != PGRES_COMMAND_OK)
			warn_or_exit_horribly(AH,
								  "could not set \"default_tablespace\" to %s: %s",
								  fmtId(want), PQerrorMessage(AH->connection));

		PQclear(res);
	}
	else
		ahprintf(AH, "%s;\n\n", qry->data);

	free(AH->currTablespace);
	AH->currTablespace = pg_strdup(want);

	destroyPQExpBuffer(qry);
}

/*
 * Set the proper default_table_access_method value for the table.
 */
static void
_selectTableAccessMethod(ArchiveHandle *AH, const char *tableam)
{
	RestoreOptions *ropt = AH->public.ropt;
	PQExpBuffer cmd;
	const char *want,
			   *have;

	/* do nothing in --no-table-access-method mode */
	if (ropt->noTableAm)
		return;

	have = AH->currTableAm;
	want = tableam;

	if (!want)
		return;

	if (have && strcmp(want, have) == 0)
		return;

	cmd = createPQExpBuffer();
	appendPQExpBuffer(cmd, "SET default_table_access_method = %s;", fmtId(want));

	if (RestoringToDB(AH))
	{
		PGresult   *res;

		res = PQexec(AH->connection, cmd->data);

		if (!res || PQresultStatus(res) != PGRES_COMMAND_OK)
			warn_or_exit_horribly(AH,
								  "could not set \"default_table_access_method\": %s",
								  PQerrorMessage(AH->connection));

		PQclear(res);
	}
	else
		ahprintf(AH, "%s\n\n", cmd->data);

	destroyPQExpBuffer(cmd);

	free(AH->currTableAm);
	AH->currTableAm = pg_strdup(want);
}


static void
_getObjectDescription(PQExpBuffer buf, const TocEntry *te)
{
	const char *type = te->desc;

	/* objects that don't require special decoration */
	if (strcmp(type, "COLLATION") == 0 ||
		strcmp(type, "CONVERSION") == 0 ||
		strcmp(type, "DOMAIN") == 0 ||
		strcmp(type, "FOREIGN TABLE") == 0 ||
		strcmp(type, "MATERIALIZED VIEW") == 0 ||
		strcmp(type, "SEQUENCE") == 0 ||
		strcmp(type, "STATISTICS") == 0 ||
		strcmp(type, "TABLE") == 0 ||
		strcmp(type, "TEXT SEARCH DICTIONARY") == 0 ||
		strcmp(type, "TEXT SEARCH CONFIGURATION") == 0 ||
		strcmp(type, "TYPE") == 0 ||
		strcmp(type, "VIEW") == 0 ||
	/* non-schema-specified objects */
		strcmp(type, "DATABASE") == 0 ||
		strcmp(type, "PROCEDURAL LANGUAGE") == 0 ||
		strcmp(type, "SCHEMA") == 0 ||
		strcmp(type, "EVENT TRIGGER") == 0 ||
		strcmp(type, "FOREIGN DATA WRAPPER") == 0 ||
		strcmp(type, "SERVER") == 0 ||
		strcmp(type, "PUBLICATION") == 0 ||
		strcmp(type, "SUBSCRIPTION") == 0)
	{
		appendPQExpBuffer(buf, "%s ", type);
		if (te->namespace && *te->namespace)
			appendPQExpBuffer(buf, "%s.", fmtId(te->namespace));
		appendPQExpBufferStr(buf, fmtId(te->tag));
	}
	/* LOs just have a name, but it's numeric so must not use fmtId */
	else if (strcmp(type, "BLOB") == 0)
	{
		appendPQExpBuffer(buf, "LARGE OBJECT %s", te->tag);
	}

	/*
	 * These object types require additional decoration.  Fortunately, the
	 * information needed is exactly what's in the DROP command.
	 */
	else if (strcmp(type, "AGGREGATE") == 0 ||
			 strcmp(type, "FUNCTION") == 0 ||
			 strcmp(type, "OPERATOR") == 0 ||
			 strcmp(type, "OPERATOR CLASS") == 0 ||
			 strcmp(type, "OPERATOR FAMILY") == 0 ||
			 strcmp(type, "PROCEDURE") == 0)
	{
		/* Chop "DROP " off the front and make a modifiable copy */
		char	   *first = pg_strdup(te->dropStmt + 5);
		char	   *last;

		/* point to last character in string */
		last = first + strlen(first) - 1;

		/* Strip off any ';' or '\n' at the end */
		while (last >= first && (*last == '\n' || *last == ';'))
			last--;
		*(last + 1) = '\0';

		appendPQExpBufferStr(buf, first);

		free(first);
		return;
	}
	/* these object types don't have separate owners */
	else if (strcmp(type, "CAST") == 0 ||
			 strcmp(type, "CHECK CONSTRAINT") == 0 ||
			 strcmp(type, "CONSTRAINT") == 0 ||
			 strcmp(type, "DATABASE PROPERTIES") == 0 ||
			 strcmp(type, "DEFAULT") == 0 ||
			 strcmp(type, "FK CONSTRAINT") == 0 ||
			 strcmp(type, "INDEX") == 0 ||
			 strcmp(type, "RULE") == 0 ||
			 strcmp(type, "TRIGGER") == 0 ||
			 strcmp(type, "ROW SECURITY") == 0 ||
			 strcmp(type, "POLICY") == 0 ||
			 strcmp(type, "USER MAPPING") == 0)
	{
		/* do nothing */
	}
	else
		pg_fatal("don't know how to set owner for object type \"%s\"", type);
}

static void
_printTableAccessMethodNoStorage(ArchiveHandle *AH, TocEntry *te)
{
	RestoreOptions *ropt = AH->public.ropt;
	const char *tableam = te->tableam;
	PQExpBuffer cmd;

	/* do nothing in --no-table-access-method mode */
	if (ropt->noTableAm)
		return;

	if (!tableam)
		return;

	Assert(te->relkind == RELKIND_PARTITIONED_TABLE);

	cmd = createPQExpBuffer();

	appendPQExpBufferStr(cmd, "ALTER TABLE ");
	appendPQExpBuffer(cmd, "%s ", fmtQualifiedId(te->namespace, te->tag));
	appendPQExpBuffer(cmd, "SET ACCESS METHOD %s;",
					  fmtId(tableam));

	if (RestoringToDB(AH))
	{
		PGresult   *res;

		res = PQexec(AH->connection, cmd->data);

		if (!res || PQresultStatus(res) != PGRES_COMMAND_OK)
			warn_or_exit_horribly(AH,
								  "could not alter table access method: %s",
								  PQerrorMessage(AH->connection));
		PQclear(res);
	}
	else
		ahprintf(AH, "%s\n\n", cmd->data);

	destroyPQExpBuffer(cmd);
}


static void
_printTocEntry(ArchiveHandle *AH, TocEntry *te, bool isData)
{
	RestoreOptions *ropt = AH->public.ropt;

	/*
	 * Select owner, schema, tablespace and default AM as necessary. The
	 * default access method for partitioned tables is handled after
	 * generating the object definition, as it requires an ALTER command
	 * rather than SET.
	 */
	_selectOutputSchema(AH, te->namespace);
	_selectTablespace(AH, te->tablespace);
	if (te->relkind != RELKIND_PARTITIONED_TABLE)
		_selectTableAccessMethod(AH, te->tableam);

	/* Emit header comment for item */
	if (!AH->noTocComments)
	{
		const char *pfx;
		char	   *sanitized_name;
		char	   *sanitized_schema;
		char	   *sanitized_owner;

		if (isData)
			pfx = "Data for ";
		else
			pfx = "";

		ahprintf(AH, "--\n");
		if (AH->public.verbose)
		{
			ahprintf(AH, "-- TOC entry %d (class %u OID %u)\n",
					 te->dumpId, te->catalogId.tableoid, te->catalogId.oid);
			if (te->nDeps > 0)
			{
				int			i;

				ahprintf(AH, "-- Dependencies:");
				for (i = 0; i < te->nDeps; i++)
					ahprintf(AH, " %d", te->dependencies[i]);
				ahprintf(AH, "\n");
			}
		}

		sanitized_name = sanitize_line(te->tag, false);
		sanitized_schema = sanitize_line(te->namespace, true);
		sanitized_owner = sanitize_line(ropt->noOwner ? NULL : te->owner, true);

		ahprintf(AH, "-- %sName: %s; Type: %s; Schema: %s; Owner: %s",
				 pfx, sanitized_name, te->desc, sanitized_schema,
				 sanitized_owner);

		free(sanitized_name);
		free(sanitized_schema);
		free(sanitized_owner);

		if (te->tablespace && strlen(te->tablespace) > 0 && !ropt->noTablespace)
		{
			char	   *sanitized_tablespace;

			sanitized_tablespace = sanitize_line(te->tablespace, false);
			ahprintf(AH, "; Tablespace: %s", sanitized_tablespace);
			free(sanitized_tablespace);
		}
		ahprintf(AH, "\n");

		if (AH->PrintExtraTocPtr != NULL)
			AH->PrintExtraTocPtr(AH, te);
		ahprintf(AH, "--\n\n");
	}

	/*
	 * Actually print the definition.  Normally we can just print the defn
	 * string if any, but we have three special cases:
	 *
	 * 1. A crude hack for suppressing AUTHORIZATION clause that old pg_dump
	 * versions put into CREATE SCHEMA.  Don't mutate the variant for schema
	 * "public" that is a comment.  We have to do this when --no-owner mode is
	 * selected.  This is ugly, but I see no other good way ...
	 *
	 * 2. BLOB METADATA entries need special processing since their defn
	 * strings are just lists of OIDs, not complete SQL commands.
	 *
	 * 3. ACL LARGE OBJECTS entries need special processing because they
	 * contain only one copy of the ACL GRANT/REVOKE commands, which we must
	 * apply to each large object listed in the associated BLOB METADATA.
	 */
	if (ropt->noOwner &&
		strcmp(te->desc, "SCHEMA") == 0 && strncmp(te->defn, "--", 2) != 0)
	{
		ahprintf(AH, "CREATE SCHEMA %s;\n\n\n", fmtId(te->tag));
	}
	else if (strcmp(te->desc, "BLOB METADATA") == 0)
	{
		IssueCommandPerBlob(AH, te, "SELECT pg_catalog.lo_create('", "')");
	}
	else if (strcmp(te->desc, "ACL") == 0 &&
			 strncmp(te->tag, "LARGE OBJECTS", 13) == 0)
	{
		IssueACLPerBlob(AH, te);
	}
	else if (te->defn && strlen(te->defn) > 0)
	{
		ahprintf(AH, "%s\n\n", te->defn);

		/*
		 * If the defn string contains multiple SQL commands, txn_size mode
		 * should count it as N actions not one.  But rather than build a full
		 * SQL parser, approximate this by counting semicolons.  One case
		 * where that tends to be badly fooled is function definitions, so
		 * ignore them.  (restore_toc_entry will count one action anyway.)
		 */
		if (ropt->txn_size > 0 &&
			strcmp(te->desc, "FUNCTION") != 0 &&
			strcmp(te->desc, "PROCEDURE") != 0)
		{
			const char *p = te->defn;
			int			nsemis = 0;

			while ((p = strchr(p, ';')) != NULL)
			{
				nsemis++;
				p++;
			}
			if (nsemis > 1)
				AH->txnCount += nsemis - 1;
		}
	}

	/*
	 * If we aren't using SET SESSION AUTH to determine ownership, we must
	 * instead issue an ALTER OWNER command.  Schema "public" is special; when
	 * a dump emits a comment in lieu of creating it, we use ALTER OWNER even
	 * when using SET SESSION for all other objects.  We assume that anything
	 * without a DROP command is not a separately ownable object.
	 */
	if (!ropt->noOwner &&
		(!ropt->use_setsessauth ||
		 (strcmp(te->desc, "SCHEMA") == 0 &&
		  strncmp(te->defn, "--", 2) == 0)) &&
		te->owner && strlen(te->owner) > 0 &&
		te->dropStmt && strlen(te->dropStmt) > 0)
	{
		if (strcmp(te->desc, "BLOB METADATA") == 0)
		{
			/* BLOB METADATA needs special code to handle multiple LOs */
			char	   *cmdEnd = psprintf(" OWNER TO %s", fmtId(te->owner));

			IssueCommandPerBlob(AH, te, "ALTER LARGE OBJECT ", cmdEnd);
			pg_free(cmdEnd);
		}
		else
		{
			/* For all other cases, we can use _getObjectDescription */
			PQExpBufferData temp;

			initPQExpBuffer(&temp);
			_getObjectDescription(&temp, te);

			/*
			 * If _getObjectDescription() didn't fill the buffer, then there
			 * is no owner.
			 */
			if (temp.data[0])
				ahprintf(AH, "ALTER %s OWNER TO %s;\n\n",
						 temp.data, fmtId(te->owner));
			termPQExpBuffer(&temp);
		}
	}

	/*
	 * Select a partitioned table's default AM, once the table definition has
	 * been generated.
	 */
	if (te->relkind == RELKIND_PARTITIONED_TABLE)
		_printTableAccessMethodNoStorage(AH, te);

	/*
	 * If it's an ACL entry, it might contain SET SESSION AUTHORIZATION
	 * commands, so we can no longer assume we know the current auth setting.
	 */
	if (_tocEntryIsACL(te))
	{
		free(AH->currUser);
		AH->currUser = NULL;
	}
}



static void
inhibit_data_for_failed_table(ArchiveHandle *AH, TocEntry *te)
{
	pg_log_info("table \"%s\" could not be created, will not restore its data",
				te->tag);

	if (AH->tableDataId[te->dumpId] != 0)
	{
		TocEntry   *ted = AH->tocsByDumpId[AH->tableDataId[te->dumpId]];

		ted->reqs = 0;
	}
}


static void
mark_create_done(ArchiveHandle *AH, TocEntry *te)
{
	if (AH->tableDataId[te->dumpId] != 0)
	{
		TocEntry   *ted = AH->tocsByDumpId[AH->tableDataId[te->dumpId]];

		ted->created = true;
	}
}


static bool
is_load_via_partition_root(TocEntry *te)
{
	if (te->defn &&
		strncmp(te->defn, "-- load via partition root ", 27) == 0)
		return true;
	if (te->copyStmt && *te->copyStmt)
	{
		PQExpBuffer copyStmt = createPQExpBuffer();
		bool		result;

		/*
		 * Build the initial part of the COPY as it would appear if the
		 * nominal target table is the actual target.  If we see anything
		 * else, it must be a load-via-partition-root case.
		 */
		appendPQExpBuffer(copyStmt, "COPY %s ",
						  fmtQualifiedId(te->namespace, te->tag));
		result = strncmp(te->copyStmt, copyStmt->data, copyStmt->len) != 0;
		destroyPQExpBuffer(copyStmt);
		return result;
	}
	/* Assume it's not load-via-partition-root */
	return false;
}


static int
restore_toc_entry(ArchiveHandle *AH, TocEntry *te, bool is_parallel)
{
	RestoreOptions *ropt = AH->public.ropt;
	int			status = WORKER_OK;
	int			reqs;
	bool		defnDumped;

	AH->currentTE = te;

	/* Dump any relevant dump warnings to stderr */
	if (!ropt->suppressDumpWarnings && strcmp(te->desc, "WARNING") == 0)
	{
		if (!ropt->dataOnly && te->defn != NULL && strlen(te->defn) != 0)
			pg_log_warning("warning from original dump file: %s", te->defn);
		else if (te->copyStmt != NULL && strlen(te->copyStmt) != 0)
			pg_log_warning("warning from original dump file: %s", te->copyStmt);
	}

	/* Work out what, if anything, we want from this entry */
	reqs = te->reqs;

	defnDumped = false;

	/*
	 * If it has a schema component that we want, then process that
	 */
	if ((reqs & REQ_SCHEMA) != 0)
	{
		bool		object_is_db = false;

		/*
		 * In --transaction-size mode, must exit our transaction block to
		 * create a database or set its properties.
		 */
		if (strcmp(te->desc, "DATABASE") == 0 ||
			strcmp(te->desc, "DATABASE PROPERTIES") == 0)
		{
			object_is_db = true;
			if (ropt->txn_size > 0)
			{
				if (AH->connection)
					CommitTransaction(&AH->public);
				else
					ahprintf(AH, "COMMIT;\n\n");
			}
		}

		/* Show namespace in log message if available */
		if (te->namespace)
			pg_log_info("creating %s \"%s.%s\"",
						te->desc, te->namespace, te->tag);
		else
			pg_log_info("creating %s \"%s\"",
						te->desc, te->tag);

		_printTocEntry(AH, te, false);
		defnDumped = true;

		if (strcmp(te->desc, "TABLE") == 0)
		{
			if (AH->lastErrorTE == te)
			{
				/*
				 * We failed to create the table. If
				 * --no-data-for-failed-tables was given, mark the
				 * corresponding TABLE DATA to be ignored.
				 *
				 * In the parallel case this must be done in the parent, so we
				 * just set the return value.
				 */
				if (ropt->noDataForFailedTables)
				{
					
					inhibit_data_for_failed_table(AH, te);
				}
			}
			else
			{
				/*
				 * We created the table successfully.  Mark the corresponding
				 * TABLE DATA for possible truncation.
				 *
				 * In the parallel case this must be done in the parent, so we
				 * just set the return value.
				 */
				
				mark_create_done(AH, te);
			}
		}

		/*
		 * If we created a DB, connect to it.  Also, if we changed DB
		 * properties, reconnect to ensure that relevant GUC settings are
		 * applied to our session.  (That also restarts the transaction block
		 * in --transaction-size mode.)
		 */
		if (object_is_db)
		{
			pg_log_info("connecting to new database \"%s\"", te->tag);
			
		}
	}

	/*
	 * If it has a data component that we want, then process that
	 */
	if ((reqs & REQ_DATA) != 0)
	{
		/*
		 * hadDumper will be set if there is genuine data component for this
		 * node. Otherwise, we need to check the defn field for statements
		 * that need to be executed in data-only restores.
		 */
		if (te->hadDumper)
		{
			/*
			 * If we can output the data, then restore it.
			 */
			if (AH->PrintTocDataPtr != NULL)
			{
				_printTocEntry(AH, te, true);

				if (strcmp(te->desc, "BLOBS") == 0 ||
					strcmp(te->desc, "BLOB COMMENTS") == 0)
				{
					pg_log_info("processing %s", te->desc);

					_selectOutputSchema(AH, "pg_catalog");

					/* Send BLOB COMMENTS data to ExecuteSimpleCommands() */
					if (strcmp(te->desc, "BLOB COMMENTS") == 0)
						AH->outputKind = OUTPUT_OTHERDATA;

					AH->PrintTocDataPtr(AH, te);

					AH->outputKind = OUTPUT_SQLCMDS;
				}
				else
				{
					bool		use_truncate;


					
					_selectOutputSchema(AH, te->namespace);

					pg_log_info("processing data for table \"%s.%s\"",
								te->namespace, te->tag);

					/*
					 * In parallel restore, if we created the table earlier in
					 * this run (so that we know it is empty) and we are not
					 * restoring a load-via-partition-root data item then we
					 * wrap the COPY in a transaction and precede it with a
					 * TRUNCATE.  If wal_level is set to minimal this prevents
					 * WAL-logging the COPY.  This obtains a speedup similar
					 * to that from using single_txn mode in non-parallel
					 * restores.
					 *
					 * We mustn't do this for load-via-partition-root cases
					 * because some data might get moved across partition
					 * boundaries, risking deadlock and/or loss of previously
					 * loaded data.  (We assume that all partitions of a
					 * partitioned table will be treated the same way.)
					 */
					use_truncate = is_parallel && te->created &&
						!is_load_via_partition_root(te);

					if (use_truncate)
					{
						/*
						 * Parallel restore is always talking directly to a
						 * server, so no need to see if we should issue BEGIN.
						 */
						StartTransaction(&AH->public);

						/*
						 * Issue TRUNCATE with ONLY so that child tables are
						 * not wiped.
						 */
						ahprintf(AH, "TRUNCATE TABLE ONLY %s;\n\n",
								 fmtQualifiedId(te->namespace, te->tag));
					}

					/*
					 * If we have a copy statement, use it.
					 */
					if (te->copyStmt && strlen(te->copyStmt) > 0)
					{
						ahprintf(AH, "%s", te->copyStmt);
						AH->outputKind = OUTPUT_COPYDATA;
					}
					else
						AH->outputKind = OUTPUT_OTHERDATA;

					AH->PrintTocDataPtr(AH, te);

					/*
					 * Terminate COPY if needed.
					 */
					if (AH->outputKind == OUTPUT_COPYDATA &&
						RestoringToDB(AH))
						
					AH->outputKind = OUTPUT_SQLCMDS;

					/* close out the transaction started above */
					if (use_truncate)
						CommitTransaction(&AH->public);

					
				}
			}
		}
		else if (!defnDumped)
		{
			/* If we haven't already dumped the defn part, do so now */
			pg_log_info("executing %s %s", te->desc, te->tag);
			_printTocEntry(AH, te, false);
		}
	}

	/*
	 * If we emitted anything for this TOC entry, that counts as one action
	 * against the transaction-size limit.  Commit if it's time to.
	 */
	if ((reqs & (REQ_SCHEMA | REQ_DATA)) != 0 && ropt->txn_size > 0)
	{
		if (++AH->txnCount >= ropt->txn_size)
		{
			if (AH->connection)
			{
				CommitTransaction(&AH->public);
				StartTransaction(&AH->public);
			}
			else
				ahprintf(AH, "COMMIT;\nBEGIN;\n\n");
			AH->txnCount = 0;
		}
	}

	if (AH->public.n_errors > 0 && status == WORKER_OK)
		status = WORKER_IGNORED_ERRORS;

	return status;
}


static void
free_keep_errno(void *p)
{
	int			save_errno = errno;

	free(p);
	errno = save_errno;
}

bool
EndCompressFileHandle(CompressFileHandle *CFH)
{
	bool		ret = false;

	if (CFH->private_data)
		ret = CFH->close_func(CFH);

	free_keep_errno(CFH);

	return ret;
}

void
DisconnectDatabase(Archive *AHX)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;
	char		errbuf[1];

	if (!AH->connection)
		return;

	if (AH->connCancel)
	{
		/*
		 * If we have an active query, send a cancel before closing, ignoring
		 * any errors.  This is of no use for a normal exit, but might be
		 * helpful during pg_fatal().
		 */
		if (PQtransactionStatus(AH->connection) == PQTRANS_ACTIVE)
			(void) PQcancel(AH->connCancel, errbuf, sizeof(errbuf));

		/*
		 * Prevent signal handler from sending a cancel after this.
		 */
		
	}

	PQfinish(AH->connection);
	AH->connection = NULL;
}

static void
RestoreOutput(ArchiveHandle *AH, CompressFileHandle *savedOutput)
{
	errno = 0;
	if (!EndCompressFileHandle(AH->OF))
		pg_fatal("could not close output file: %m");

	AH->OF = savedOutput;
}

void
RestoreArchive(Archive *AHX)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;
	RestoreOptions *ropt = AH->public.ropt;
	bool		parallel_mode;
	TocEntry   *te;
	CompressFileHandle *sav;

	AH->stage = STAGE_INITIALIZING;

	/*
	 * If we're going to do parallel restore, there are some restrictions.
	 */
	parallel_mode = 0;
	
	/*
	 * Make sure we won't need (de)compression we haven't got
	 */
	if (AH->PrintTocDataPtr != NULL)
	{
		for (te = AH->toc->next; te != AH->toc; te = te->next)
		{
			if (te->hadDumper && (te->reqs & REQ_DATA) != 0)
			{
				char	   *errmsg = NULL;

				if (errmsg)
					pg_fatal("cannot restore from compressed archive (%s)",
							 errmsg);
				else
					break;
			}
		}
	}

	/*
	 * Prepare index arrays, so we can assume we have them throughout restore.
	 * It's possible we already did this, though.
	 */
	if (AH->tocsByDumpId == NULL)
		buildTocEntryArrays(AH);

	/*
	 * If we're using a DB connection, then connect it.
	 */
	if (ropt->useDB)
	{
		pg_log_info("connecting to database for restore");
		if (AH->version < K_VERS_1_3)
			pg_fatal("direct database connections are not supported in pre-1.3 archives");

		/*
		 * We don't want to guess at whether the dump will successfully
		 * restore; allow the attempt regardless of the version of the restore
		 * target.
		 */
		AHX->minRemoteVersion = 0;
		AHX->maxRemoteVersion = 9999999;

		ConnectDatabase(AHX, &ropt->cparams, false);

		/*
		 * If we're talking to the DB directly, don't send comments since they
		 * obscure SQL when displaying errors
		 */
		AH->noTocComments = 1;
	}

	/*
	 * Work out if we have an implied data-only restore. This can happen if
	 * the dump was data only or if the user has used a toc list to exclude
	 * all of the schema data. All we do is look for schema entries - if none
	 * are found then we set the dataOnly flag.
	 *
	 * We could scan for wanted TABLE entries, but that is not the same as
	 * dataOnly. At this stage, it seems unnecessary (6-Mar-2001).
	 */
	if (!ropt->dataOnly)
	{
		int			impliedDataOnly = 1;

		for (te = AH->toc->next; te != AH->toc; te = te->next)
		{
			if ((te->reqs & REQ_SCHEMA) != 0)
			{					/* It's schema, and it's wanted */
				impliedDataOnly = 0;
				break;
			}
		}
		if (impliedDataOnly)
		{
			ropt->dataOnly = impliedDataOnly;
			pg_log_info("implied data-only restore");
		}
	}

	/*
	 * Setup the output file if necessary.
	 */
	sav = SaveOutput(AH);
	if (ropt->filename || ropt->compression_spec.algorithm != PG_COMPRESSION_NONE)
		SetOutput(AH, ropt->filename, ropt->compression_spec);

	ahprintf(AH, "--\n-- PostgreSQL database dump\n--\n\n");

	if (AH->archiveRemoteVersion)
		ahprintf(AH, "-- Dumped from database version %s\n",
				 AH->archiveRemoteVersion);
	if (AH->archiveDumpVersion)
		ahprintf(AH, "-- Dumped by pg_ozerki version %s\n",
				 AH->archiveDumpVersion);

	ahprintf(AH, "\n");

	if (AH->public.verbose)
		dumpTimestamp(AH, "Started on", AH->createDate);

	if (ropt->single_txn)
	{
		if (AH->connection)
			StartTransaction(AHX);
		else
			ahprintf(AH, "BEGIN;\n\n");
	}

	/*
	 * Establish important parameter values right away.
	 */
	_doSetFixedOutputState(AH);

	AH->stage = STAGE_PROCESSING;

	/*
	 * Drop the items at the start, in reverse order
	 */
	if (ropt->dropSchema)
	{
		for (te = AH->toc->prev; te != AH->toc; te = te->prev)
		{
			AH->currentTE = te;

			/*
			 * In createDB mode, issue a DROP *only* for the database as a
			 * whole.  Issuing drops against anything else would be wrong,
			 * because at this point we're connected to the wrong database.
			 * (The DATABASE PROPERTIES entry, if any, should be treated like
			 * the DATABASE entry.)
			 */
			if (ropt->createDB)
			{
				if (strcmp(te->desc, "DATABASE") != 0 &&
					strcmp(te->desc, "DATABASE PROPERTIES") != 0)
					continue;
			}

			/* Otherwise, drop anything that's selected and has a dropStmt */
			if (((te->reqs & (REQ_SCHEMA | REQ_DATA)) != 0) && te->dropStmt)
			{
				bool		not_allowed_in_txn = false;

				pg_log_info("dropping %s %s", te->desc, te->tag);

				/*
				 * In --transaction-size mode, we have to temporarily exit our
				 * transaction block to drop objects that can't be dropped
				 * within a transaction.
				 */
				if (ropt->txn_size > 0)
				{
					if (strcmp(te->desc, "DATABASE") == 0 ||
						strcmp(te->desc, "DATABASE PROPERTIES") == 0)
					{
						not_allowed_in_txn = true;
						if (AH->connection)
							CommitTransaction(AHX);
						else
							ahprintf(AH, "COMMIT;\n");
					}
				}

				/* Select owner and schema as necessary */
				_selectOutputSchema(AH, te->namespace);

				/*
				 * Now emit the DROP command, if the object has one.  Note we
				 * don't necessarily emit it verbatim; at this point we add an
				 * appropriate IF EXISTS clause, if the user requested it.
				 */
				if (strcmp(te->desc, "BLOB METADATA") == 0)
				{
					/* We must generate the per-blob commands */
					if (ropt->if_exists)
						IssueCommandPerBlob(AH, te,
											"SELECT pg_catalog.lo_unlink(oid) "
											"FROM pg_catalog.pg_largeobject_metadata "
											"WHERE oid = '", "'");
					else
						IssueCommandPerBlob(AH, te,
											"SELECT pg_catalog.lo_unlink('",
											"')");
				}
				else if (*te->dropStmt != '\0')
				{
					if (!ropt->if_exists ||
						strncmp(te->dropStmt, "--", 2) == 0)
					{
						/*
						 * Without --if-exists, or if it's just a comment (as
						 * happens for the public schema), print the dropStmt
						 * as-is.
						 */
						ahprintf(AH, "%s", te->dropStmt);
					}
					else
					{
						/*
						 * Inject an appropriate spelling of "if exists".  For
						 * old-style large objects, we have a routine that
						 * knows how to do it, without depending on
						 * te->dropStmt; use that.  For other objects we need
						 * to parse the command.
						 */
						if (strcmp(te->desc, "BLOB") == 0)
						{
							DropLOIfExists(AH, te->catalogId.oid);
						}
						else
						{
							char	   *dropStmt = pg_strdup(te->dropStmt);
							char	   *dropStmtOrig = dropStmt;
							PQExpBuffer ftStmt = createPQExpBuffer();

							/*
							 * Need to inject IF EXISTS clause after ALTER
							 * TABLE part in ALTER TABLE .. DROP statement
							 */
							if (strncmp(dropStmt, "ALTER TABLE", 11) == 0)
							{
								appendPQExpBufferStr(ftStmt,
													 "ALTER TABLE IF EXISTS");
								dropStmt = dropStmt + 11;
							}

							/*
							 * ALTER TABLE..ALTER COLUMN..DROP DEFAULT does
							 * not support the IF EXISTS clause, and therefore
							 * we simply emit the original command for DEFAULT
							 * objects (modulo the adjustment made above).
							 *
							 * Likewise, don't mess with DATABASE PROPERTIES.
							 *
							 * If we used CREATE OR REPLACE VIEW as a means of
							 * quasi-dropping an ON SELECT rule, that should
							 * be emitted unchanged as well.
							 *
							 * For other object types, we need to extract the
							 * first part of the DROP which includes the
							 * object type.  Most of the time this matches
							 * te->desc, so search for that; however for the
							 * different kinds of CONSTRAINTs, we know to
							 * search for hardcoded "DROP CONSTRAINT" instead.
							 */
							if (strcmp(te->desc, "DEFAULT") == 0 ||
								strcmp(te->desc, "DATABASE PROPERTIES") == 0 ||
								strncmp(dropStmt, "CREATE OR REPLACE VIEW", 22) == 0)
								appendPQExpBufferStr(ftStmt, dropStmt);
							else
							{
								char		buffer[40];
								char	   *mark;

								if (strcmp(te->desc, "CONSTRAINT") == 0 ||
									strcmp(te->desc, "CHECK CONSTRAINT") == 0 ||
									strcmp(te->desc, "FK CONSTRAINT") == 0)
									strcpy(buffer, "DROP CONSTRAINT");
								else
									snprintf(buffer, sizeof(buffer), "DROP %s",
											 te->desc);

								mark = strstr(dropStmt, buffer);

								if (mark)
								{
									*mark = '\0';
									appendPQExpBuffer(ftStmt, "%s%s IF EXISTS%s",
													  dropStmt, buffer,
													  mark + strlen(buffer));
								}
								else
								{
									/* complain and emit unmodified command */
									pg_log_warning("could not find where to insert IF EXISTS in statement \"%s\"",
												   dropStmtOrig);
									appendPQExpBufferStr(ftStmt, dropStmt);
								}
							}

							ahprintf(AH, "%s", ftStmt->data);

							destroyPQExpBuffer(ftStmt);
							pg_free(dropStmtOrig);
						}
					}
				}

				/*
				 * In --transaction-size mode, re-establish the transaction
				 * block if needed; otherwise, commit after every N drops.
				 */
				if (ropt->txn_size > 0)
				{
					if (not_allowed_in_txn)
					{
						if (AH->connection)
							StartTransaction(AHX);
						else
							ahprintf(AH, "BEGIN;\n");
						AH->txnCount = 0;
					}
					else if (++AH->txnCount >= ropt->txn_size)
					{
						if (AH->connection)
						{
							CommitTransaction(AHX);
							StartTransaction(AHX);
						}
						else
							ahprintf(AH, "COMMIT;\nBEGIN;\n");
						AH->txnCount = 0;
					}
				}
			}
		}

		/*
		 * _selectOutputSchema may have set currSchema to reflect the effect
		 * of a "SET search_path" command it emitted.  However, by now we may
		 * have dropped that schema; or it might not have existed in the first
		 * place.  In either case the effective value of search_path will not
		 * be what we think.  Forcibly reset currSchema so that we will
		 * re-establish the search_path setting when needed (after creating
		 * the schema).
		 *
		 * If we treated users as pg_dump'able objects then we'd need to reset
		 * currUser here too.
		 */
		free(AH->currSchema);
		AH->currSchema = NULL;
	}

	
	
	/*
		* In serial mode, process everything in three phases: normal items,
		* then ACLs, then post-ACL items.  We might be able to skip one or
		* both extra phases in some cases, eg data-only restores.
		*/
	bool		haveACL = false;
	bool		havePostACL = false;

	for (te = AH->toc->next; te != AH->toc; te = te->next)
	{
		if ((te->reqs & (REQ_SCHEMA | REQ_DATA)) == 0)
			continue;		/* ignore if not to be dumped at all */

		switch (_tocEntryRestorePass(te))
		{
			case RESTORE_PASS_MAIN:
				(void) restore_toc_entry(AH, te, false);
				break;
			case RESTORE_PASS_ACL:
				haveACL = true;
				break;
			case RESTORE_PASS_POST_ACL:
				havePostACL = true;
				break;
		}
	}

	if (haveACL)
	{
		for (te = AH->toc->next; te != AH->toc; te = te->next)
		{
			if ((te->reqs & (REQ_SCHEMA | REQ_DATA)) != 0 &&
				_tocEntryRestorePass(te) == RESTORE_PASS_ACL) {
				//(void) restore_toc_entry(AH, te, false);
				}
		}
	}

	if (havePostACL)
	{
		for (te = AH->toc->next; te != AH->toc; te = te->next)
		{
			if ((te->reqs & (REQ_SCHEMA | REQ_DATA)) != 0 &&
				_tocEntryRestorePass(te) == RESTORE_PASS_POST_ACL){
				//(void) restore_toc_entry(AH, te, false);
				}
		}
	}
	

	/*
	 * Close out any persistent transaction we may have.  While these two
	 * cases are started in different places, we can end both cases here.
	 */
	if (ropt->single_txn || ropt->txn_size > 0)
	{
		if (AH->connection)
			CommitTransaction(AHX);
		else
			ahprintf(AH, "COMMIT;\n\n");
	}

	if (AH->public.verbose)
		dumpTimestamp(AH, "Completed on", time(NULL));

	ahprintf(AH, "--\n-- PostgreSQL database dump complete\n--\n\n");

	/*
	 * Clean up & we're done.
	 */
	AH->stage = STAGE_FINALIZING;

	if (ropt->filename || ropt->compression_spec.algorithm != PG_COMPRESSION_NONE)
		RestoreOutput(AH, sav);

	if (ropt->useDB)
		DisconnectDatabase(&AH->public);
}


void
CloseArchive(Archive *AHX)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;

	AH->ClosePtr(AH);

	/* Close the output */
	errno = 0;
	if (!EndCompressFileHandle(AH->OF))
		pg_fatal("could not close output file: %m");
}


void
expand_table_name_patterns(Archive *fout,
						   SimpleStringList *patterns, SimpleOidList *oids,
						   bool strict_names, bool with_child_tables)
{
	PQExpBuffer query;
	PGresult   *res;
	SimpleStringListCell *cell;
	int			i;

	if (patterns->head == NULL)
		return;					/* nothing to do */

	query = createPQExpBuffer();

	/*
	 * this might sometimes result in duplicate entries in the OID list, but
	 * we don't care.
	 */

	for (cell = patterns->head; cell; cell = cell->next)
	{
		PQExpBufferData dbbuf;
		int			dotcnt;

		/*
		 * Query must remain ABSOLUTELY devoid of unqualified names.  This
		 * would be unnecessary given a pg_table_is_visible() variant taking a
		 * search_path argument.
		 *
		 * For with_child_tables, we start with the basic query's results and
		 * recursively search the inheritance tree to add child tables.
		 */
		if (with_child_tables)
		{
			appendPQExpBuffer(query, "WITH RECURSIVE partition_tree (relid) AS (\n");
		}

		appendPQExpBuffer(query,
						  "SELECT c.oid"
						  "\nFROM pg_catalog.pg_class c"
						  "\n     LEFT JOIN pg_catalog.pg_namespace n"
						  "\n     ON n.oid OPERATOR(pg_catalog.=) c.relnamespace"
						  "\nWHERE c.relkind OPERATOR(pg_catalog.=) ANY"
						  "\n    (array['%c', '%c', '%c', '%c', '%c', '%c'])\n",
						  RELKIND_RELATION, RELKIND_SEQUENCE, RELKIND_VIEW,
						  RELKIND_MATVIEW, RELKIND_FOREIGN_TABLE,
						  RELKIND_PARTITIONED_TABLE);
		initPQExpBuffer(&dbbuf);
		processSQLNamePattern(GetConnection(fout), query, cell->val, true,
							  false, "n.nspname", "c.relname", NULL,
							  "pg_catalog.pg_table_is_visible(c.oid)", &dbbuf,
							  &dotcnt);
		if (dotcnt > 2)
			pg_fatal("improper relation name (too many dotted names): %s",
					 cell->val);
		else if (dotcnt == 2)
			prohibit_crossdb_refs(GetConnection(fout), dbbuf.data, cell->val);
		termPQExpBuffer(&dbbuf);

		if (with_child_tables)
		{
			appendPQExpBuffer(query, "UNION"
							  "\nSELECT i.inhrelid"
							  "\nFROM partition_tree p"
							  "\n     JOIN pg_catalog.pg_inherits i"
							  "\n     ON p.relid OPERATOR(pg_catalog.=) i.inhparent"
							  "\n)"
							  "\nSELECT relid FROM partition_tree");
		}

		ExecuteSqlStatement(fout, "RESET search_path");
		res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);
		//PQclear(ExecuteSqlQueryForSingleRow(fout,
											//ALWAYS_SECURE_SEARCH_PATH_SQL));
		if (strict_names && PQntuples(res) == 0)
			pg_fatal("no matching tables were found for pattern \"%s\"", cell->val);

		for (i = 0; i < PQntuples(res); i++)
		{
			simple_oid_list_append(oids, atooid(PQgetvalue(res, i, 0)));
		}

		PQclear(res);
		resetPQExpBuffer(query);
	}

	destroyPQExpBuffer(query);
}


static void
prohibit_crossdb_refs(PGconn *conn, const char *dbname, const char *pattern)
{
	const char *db;

	db = PQdb(conn);
	if (db == NULL)
		pg_fatal("You are currently not connected to a database.");

	if (strcmp(db, dbname) != 0)
		pg_fatal("cross-database references are not implemented: %s",
				 pattern);
}



char* get_planner_settings(Archive* AH){


	int i_name;
	int i_setting;
	int i_unit;

	const char* query = "SELECT name, setting, unit "
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

	PGresult* res = ExecuteSqlQuery(
		(AH), query, PGRES_TUPLES_OK);

	i_name = PQfnumber(res, "name");
	i_setting = PQfnumber(res, "setting");
	i_unit = PQfnumber(res, "unit");

	PQExpBuffer buf = createPQExpBuffer();
	for (int i = 0; i < 6; i++) {
		char* name = PQgetvalue(res, i, i_name);
		char* setting = PQgetvalue(res, i, i_setting);
		char* unit = PQgetvalue(res, i, i_unit);
		{
                
                if (unit && strcmp(unit, "") != 0)
                {
                    
                    appendPQExpBuffer(buf, "ALTER SYSTEM SET %s = '%s%s';\n", 
                                   name, setting, unit);
                }
                else
                {
                    appendPQExpBuffer(buf, "ALTER SYSTEM SET %s = %s;\n", 
                                   name, setting);
                }
                
                // pfree(name);
                // pfree(setting);
                // if (unit) pfree(unit);
            } 
            
	}

	char* ret = pg_strdup(buf->data);
	return ret;
}