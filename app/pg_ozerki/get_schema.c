#include "pg_ozerki.h"

#include "postgres_fe.h"
#include <stdlib.h>
#include <ctype.h>
#include "fe_utils/string_utils.h"
#include "catalog/pg_class_d.h"
#include "catalog/pg_collation_d.h"
#include "catalog/pg_extension_d.h"
#include "catalog/pg_namespace_d.h"
#include "catalog/pg_operator_d.h"
#include "catalog/pg_proc_d.h"
#include "catalog/pg_publication_d.h"
#include "catalog/pg_subscription_d.h"
#include "catalog/pg_type_d.h"
#include "common/hashfn.h"
#include "access/transam.h"

#define SH_PREFIX		catalogid
#define SH_ELEMENT_TYPE	CatalogIdMapEntry
#define SH_KEY_TYPE		CatalogId
#define	SH_KEY			catId
#define SH_HASH_KEY(tb, key)	hash_bytes((const unsigned char *) &(key), sizeof(CatalogId))
#define SH_EQUAL(tb, a, b)		((a).oid == (b).oid && (a).tableoid == (b).tableoid)
#define SH_STORE_HASH
#define SH_GET_HASH(tb, a) (a)->hashval
#define	SH_SCOPE		static inline
#define SH_RAW_ALLOCATOR	pg_malloc0
#define SH_DECLARE
#define SH_DEFINE
#include "lib/simplehash.h"

#define CATALOGIDHASH_INITIAL_SIZE	10000



const char *progname = "pg_ozerki";

const CatalogId nilCatalogId = {0, 0};

static catalogid_hash *catalogIdHash = NULL;

static DumpableObject **dumpIdMap = NULL;
static int	allocedDumpIds = 0;
static DumpId lastDumpId = 0;	/* Note: 0 is InvalidDumpId */


static Oid	g_last_builtin_oid = FirstNormalObjectId - 1; 


static RoleNameItem *rolenames = NULL;
static int	nrolenames = 0;

enum dbObjectTypePriorities
{
	PRIO_NAMESPACE = 1,
	PRIO_PROCLANG,
	PRIO_COLLATION,
	PRIO_TRANSFORM,
	PRIO_EXTENSION,
	PRIO_TYPE,					/* used for DO_TYPE and DO_SHELL_TYPE */
	PRIO_CAST,
	PRIO_FUNC,
	PRIO_AGG,
	PRIO_ACCESS_METHOD,
	PRIO_OPERATOR,
	PRIO_OPFAMILY,				/* used for DO_OPFAMILY and DO_OPCLASS */
	PRIO_CONVERSION,
	PRIO_TSPARSER,
	PRIO_TSTEMPLATE,
	PRIO_TSDICT,
	PRIO_TSCONFIG,
	PRIO_FDW,
	PRIO_FOREIGN_SERVER,
	PRIO_TABLE,
	PRIO_TABLE_ATTACH,
	PRIO_DUMMY_TYPE,
	PRIO_ATTRDEF,
	PRIO_LARGE_OBJECT,
	PRIO_PRE_DATA_BOUNDARY,		/* boundary! */
	PRIO_TABLE_DATA,
	PRIO_SEQUENCE_SET,
	PRIO_LARGE_OBJECT_DATA,
	PRIO_POST_DATA_BOUNDARY,	/* boundary! */
	PRIO_CONSTRAINT,
	PRIO_INDEX,
	PRIO_INDEX_ATTACH,
	PRIO_STATSEXT,
	PRIO_RULE,
	PRIO_TRIGGER,
	PRIO_FK_CONSTRAINT,
	PRIO_POLICY,
	PRIO_PUBLICATION,
	PRIO_PUBLICATION_REL,
	PRIO_PUBLICATION_TABLE_IN_SCHEMA,
	PRIO_SUBSCRIPTION,
	PRIO_SUBSCRIPTION_REL,
	PRIO_DEFAULT_ACL,			/* done in ACL pass */
	PRIO_EVENT_TRIGGER,			/* must be next to last! */
	PRIO_REFRESH_MATVIEW		/* must be last! */
};

static const int dbObjectTypePriority[] =
{
	[DO_NAMESPACE] = PRIO_NAMESPACE,
	[DO_EXTENSION] = PRIO_EXTENSION,
	[DO_TYPE] = PRIO_TYPE,
	[DO_SHELL_TYPE] = PRIO_TYPE,
	[DO_FUNC] = PRIO_FUNC,
	[DO_AGG] = PRIO_AGG,
	[DO_OPERATOR] = PRIO_OPERATOR,
	[DO_ACCESS_METHOD] = PRIO_ACCESS_METHOD,
	[DO_OPCLASS] = PRIO_OPFAMILY,
	[DO_OPFAMILY] = PRIO_OPFAMILY,
	[DO_COLLATION] = PRIO_COLLATION,
	[DO_CONVERSION] = PRIO_CONVERSION,
	[DO_TABLE] = PRIO_TABLE,
	[DO_TABLE_ATTACH] = PRIO_TABLE_ATTACH,
	[DO_ATTRDEF] = PRIO_ATTRDEF,
	[DO_INDEX] = PRIO_INDEX,
	[DO_INDEX_ATTACH] = PRIO_INDEX_ATTACH,
	[DO_STATSEXT] = PRIO_STATSEXT,
	[DO_RULE] = PRIO_RULE,
	[DO_TRIGGER] = PRIO_TRIGGER,
	[DO_CONSTRAINT] = PRIO_CONSTRAINT,
	[DO_FK_CONSTRAINT] = PRIO_FK_CONSTRAINT,
	[DO_PROCLANG] = PRIO_PROCLANG,
	[DO_CAST] = PRIO_CAST,
	[DO_TABLE_DATA] = PRIO_TABLE_DATA,
	[DO_SEQUENCE_SET] = PRIO_SEQUENCE_SET,
	[DO_DUMMY_TYPE] = PRIO_DUMMY_TYPE,
	[DO_TSPARSER] = PRIO_TSPARSER,
	[DO_TSDICT] = PRIO_TSDICT,
	[DO_TSTEMPLATE] = PRIO_TSTEMPLATE,
	[DO_TSCONFIG] = PRIO_TSCONFIG,
	[DO_FDW] = PRIO_FDW,
	[DO_FOREIGN_SERVER] = PRIO_FOREIGN_SERVER,
	[DO_DEFAULT_ACL] = PRIO_DEFAULT_ACL,
	[DO_TRANSFORM] = PRIO_TRANSFORM,
	[DO_LARGE_OBJECT] = PRIO_LARGE_OBJECT,
	[DO_LARGE_OBJECT_DATA] = PRIO_LARGE_OBJECT_DATA,
	[DO_PRE_DATA_BOUNDARY] = PRIO_PRE_DATA_BOUNDARY,
	[DO_POST_DATA_BOUNDARY] = PRIO_POST_DATA_BOUNDARY,
	[DO_EVENT_TRIGGER] = PRIO_EVENT_TRIGGER,
	[DO_REFRESH_MATVIEW] = PRIO_REFRESH_MATVIEW,
	[DO_POLICY] = PRIO_POLICY,
	[DO_PUBLICATION] = PRIO_PUBLICATION,
	[DO_PUBLICATION_REL] = PRIO_PUBLICATION_REL,
	[DO_PUBLICATION_TABLE_IN_SCHEMA] = PRIO_PUBLICATION_TABLE_IN_SCHEMA,
	[DO_SUBSCRIPTION] = PRIO_SUBSCRIPTION,
	[DO_SUBSCRIPTION_REL] = PRIO_SUBSCRIPTION_REL,
};


TableInfo *
getSchemaData(Archive *fout, int *numTablesPtr, QueryDependencies* deps)
{
	TableInfo  *tblinfo;
	ExtensionInfo *extinfo;
	InhInfo    *inhinfo;
	int			numTables;
	int			numTypes;
	int			numFuncs;
	int			numOperators;
	int			numCollations;
	int			numNamespaces;
	int			numExtensions;
	int			numPublications;
	int			numAggregates;
	int			numInherits;
	int			numRules;
	int			numProcLangs;
	int			numCasts;
	int			numTransforms;
	int			numAccessMethods;
	int			numOpclasses;
	int			numOpfamilies;
	int			numConversions;
	int			numTSParsers;
	int			numTSTemplates;
	int			numTSDicts;
	int			numTSConfigs;
	int			numForeignDataWrappers;
	int			numForeignServers;
	int			numDefaultACLs;
	int			numEventTriggers;

	/*
	 * We must read extensions and extension membership info first, because
	 * extension membership needs to be consultable during decisions about
	 * whether other objects are to be dumped.
	 */

	pg_log_info("reading extensions");
	extinfo = getExtensions(fout, &numExtensions);
	
	pg_log_info("identifying extension members");
	getExtensionMembership(fout, extinfo, numExtensions);
	
	pg_log_info("reading schemas");
	(void) getNamespaces(fout, &numNamespaces);
	
	/*
	 * getTables should be done as soon as possible, so as to minimize the
	 * window between starting our transaction and acquiring per-table locks.
	 * However, we have to do getNamespaces first because the tables get
	 * linked to their containing namespaces during getTables.
	 */

	if (!(deps->been_analyzed && deps->tableCount == 0)) {
		pg_log_info("reading user-defined tables");
		tblinfo = getTables(fout, &numTables);
		
		getOwnedSeqs(fout, tblinfo, numTables);

		pg_log_info("reading user-defined functions");
		(void) getFuncs(fout, &numFuncs);

		/* this must be after getTables and getFuncs */
		pg_log_info("reading user-defined types");
		(void) getTypes(fout, &numTypes);

		/* this must be after getFuncs, too */
		pg_log_info("reading procedural languages");
		getProcLangs(fout, &numProcLangs);

		pg_log_info("reading user-defined aggregate functions");
		getAggregates(fout, &numAggregates);

		pg_log_info("reading user-defined operators");
		(void) getOperators(fout, &numOperators);

		pg_log_info("reading user-defined access methods");
		getAccessMethods(fout, &numAccessMethods);

		pg_log_info("reading user-defined operator classes");
		getOpclasses(fout, &numOpclasses);
		
		pg_log_info("reading user-defined operator families");
		getOpfamilies(fout, &numOpfamilies);

		pg_log_info("reading user-defined text search parsers");
		getTSParsers(fout, &numTSParsers);

		pg_log_info("reading user-defined text search templates");
		getTSTemplates(fout, &numTSTemplates);

		pg_log_info("reading user-defined text search dictionaries");
		getTSDictionaries(fout, &numTSDicts);

		pg_log_info("reading user-defined text search configurations");
		getTSConfigurations(fout, &numTSConfigs);

		pg_log_info("reading user-defined foreign-data wrappers");
		getForeignDataWrappers(fout, &numForeignDataWrappers);

		pg_log_info("reading user-defined foreign servers");
		getForeignServers(fout, &numForeignServers);

		pg_log_info("reading default privileges");
		getDefaultACLs(fout, &numDefaultACLs);

		pg_log_info("reading user-defined collations");
		(void) getCollations(fout, &numCollations);

		pg_log_info("reading user-defined conversions");
		getConversions(fout, &numConversions);

		pg_log_info("reading type casts");
		getCasts(fout, &numCasts);

		pg_log_info("reading transforms");
		getTransforms(fout, &numTransforms);

		

		pg_log_info("reading table inheritance information");
		inhinfo = getInherits(fout, &numInherits);

		pg_log_info("reading event triggers");
		getEventTriggers(fout, &numEventTriggers);

		/* Identify extension configuration tables that should be dumped */
		pg_log_info("finding extension tables");
		processExtensionTables(fout, extinfo, numExtensions);

		/* Link tables to parents, mark parents of target tables interesting */
		pg_log_info("finding inheritance relationships");
		flagInhTables(fout, tblinfo, numTables, inhinfo, numInherits);

		pg_log_info("reading column info for interesting tables");
		getTableAttrs(fout, tblinfo, numTables);

		pg_log_info("flagging inherited columns in subtables");
		flagInhAttrs(fout, tblinfo, numTables);

		pg_log_info("reading partitioning data");
		getPartitioningInfo(fout);

		pg_log_info("reading indexes");
		getIndexes(fout, tblinfo, numTables);

		pg_log_info("flagging indexes in partitioned tables");
		flagInhIndexes(fout, tblinfo, numTables);

		pg_log_info("reading extended statistics");
		getExtendedStatistics(fout);

		pg_log_info("reading constraints");
		getConstraints(fout, tblinfo, numTables);

		pg_log_info("reading triggers");
		getTriggers(fout, tblinfo, numTables);

		pg_log_info("reading rewrite rules");
		getRules(fout, &numRules);

		pg_log_info("reading policies");
		getPolicies(fout, tblinfo, numTables);

		pg_log_info("reading publications");
		(void) getPublications(fout, &numPublications);

		pg_log_info("reading publication membership of tables");
		getPublicationTables(fout, tblinfo, numTables);

		pg_log_info("reading publication membership of schemas");
		getPublicationNamespaces(fout);

		pg_log_info("reading subscriptions");
		getSubscriptions(fout);

		pg_log_info("reading subscription membership of tables");
		getSubscriptionTables(fout);

		pg_log_info("marking views for dump by query");
		mark_views_for_dump(tblinfo, numTables, deps);

		pg_log_info("marking schemas for dump by query");
		mark_schemas_for_dump(tblinfo, numTables);
		free(inhinfo);				/* not needed any longer */
	} else {
		numTables = 0;
	}
	*numTablesPtr = numTables;
	return tblinfo;
}





NamespaceInfo *
getNamespaces(Archive *fout, int *numNamespaces)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	NamespaceInfo *nsinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_nspname;
	int			i_nspowner;
	int			i_nspacl;
	int			i_acldefault;

	query = createPQExpBuffer();

	/*
	 * we fetch all namespaces including system ones, so that every object we
	 * read in can be linked to a containing namespace.
	 */
	appendPQExpBufferStr(query, "SELECT n.tableoid, n.oid, n.nspname, "
						 "n.nspowner, "
						 "n.nspacl, "
						 "acldefault('n', n.nspowner) AS acldefault "
						 "FROM pg_namespace n");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	nsinfo = (NamespaceInfo *) pg_malloc(ntups * sizeof(NamespaceInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_nspname = PQfnumber(res, "nspname");
	i_nspowner = PQfnumber(res, "nspowner");
	i_nspacl = PQfnumber(res, "nspacl");
	i_acldefault = PQfnumber(res, "acldefault");

	for (i = 0; i < ntups; i++)
	{
		const char *nspowner;

		nsinfo[i].dobj.objType = DO_NAMESPACE;
		nsinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		nsinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&nsinfo[i].dobj);
		nsinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_nspname));
		nsinfo[i].dacl.acl = pg_strdup(PQgetvalue(res, i, i_nspacl));
		nsinfo[i].dacl.acldefault = pg_strdup(PQgetvalue(res, i, i_acldefault));
		nsinfo[i].dacl.privtype = 0;
		nsinfo[i].dacl.initprivs = NULL;
		nspowner = PQgetvalue(res, i, i_nspowner);
		nsinfo[i].nspowner = atooid(nspowner);
		nsinfo[i].rolname = getRoleName(nspowner);

		/* Decide whether to dump this namespace */
		selectDumpableNamespace(&nsinfo[i], fout);

		/* Mark whether namespace has an ACL */
		if (!PQgetisnull(res, i, i_nspacl))
			nsinfo[i].dobj.components |= DUMP_COMPONENT_ACL;

		/*
		 * We ignore any pg_init_privs.initprivs entry for the public schema
		 * and assume a predetermined default, for several reasons.  First,
		 * dropping and recreating the schema removes its pg_init_privs entry,
		 * but an empty destination database starts with this ACL nonetheless.
		 * Second, we support dump/reload of public schema ownership changes.
		 * ALTER SCHEMA OWNER filters nspacl through aclnewowner(), but
		 * initprivs continues to reflect the initial owner.  Hence,
		 * synthesize the value that nspacl will have after the restore's
		 * ALTER SCHEMA OWNER.  Third, this makes the destination database
		 * match the source's ACL, even if the latter was an initdb-default
		 * ACL, which changed in v15.  An upgrade pulls in changes to most
		 * system object ACLs that the DBA had not customized.  We've made the
		 * public schema depart from that, because changing its ACL so easily
		 * breaks applications.
		 */
		if (strcmp(nsinfo[i].dobj.name, "public") == 0)
		{
			PQExpBuffer aclarray = createPQExpBuffer();
			PQExpBuffer aclitem = createPQExpBuffer();

			/* Standard ACL as of v15 is {owner=UC/owner,=U/owner} */
			appendPQExpBufferChar(aclarray, '{');
			quoteAclUserName(aclitem, nsinfo[i].rolname);
			appendPQExpBufferStr(aclitem, "=UC/");
			quoteAclUserName(aclitem, nsinfo[i].rolname);
			appendPGArray(aclarray, aclitem->data);
			resetPQExpBuffer(aclitem);
			appendPQExpBufferStr(aclitem, "=U/");
			quoteAclUserName(aclitem, nsinfo[i].rolname);
			appendPGArray(aclarray, aclitem->data);
			appendPQExpBufferChar(aclarray, '}');

			nsinfo[i].dacl.privtype = 'i';
			nsinfo[i].dacl.initprivs = pstrdup(aclarray->data);
			nsinfo[i].dobj.components |= DUMP_COMPONENT_ACL;

			destroyPQExpBuffer(aclarray);
			destroyPQExpBuffer(aclitem);
		}
	}

	PQclear(res);
	destroyPQExpBuffer(query);

	*numNamespaces = ntups;

	return nsinfo;
}

/*
 * findNamespace:
 *		given a namespace OID, look up the info read by getNamespaces
 */
static NamespaceInfo *
findNamespace(Oid nsoid)
{
	NamespaceInfo *nsinfo;

	nsinfo = findNamespaceByOid(nsoid);
	if (nsinfo == NULL)
		pg_fatal("schema with OID %u does not exist", nsoid);
	return nsinfo;
}

/*
 * getExtensions:
 *	  read all extensions in the system catalogs and return them in the
 * ExtensionInfo* structure
 *
 *	numExtensions is set to the number of extensions read in
 */
ExtensionInfo *
getExtensions(Archive *fout, int *numExtensions)
{
	DumpOptions *dopt = fout->dopt;
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	ExtensionInfo *extinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_extname;
	int			i_nspname;
	int			i_extrelocatable;
	int			i_extversion;
	int			i_extconfig;
	int			i_extcondition;

	query = createPQExpBuffer();

	appendPQExpBufferStr(query, "SELECT x.tableoid, x.oid, "
						 "x.extname, n.nspname, x.extrelocatable, x.extversion, x.extconfig, x.extcondition "
						 "FROM pg_extension x "
						 "JOIN pg_namespace n ON n.oid = x.extnamespace");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	extinfo = (ExtensionInfo *) pg_malloc(ntups * sizeof(ExtensionInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_extname = PQfnumber(res, "extname");
	i_nspname = PQfnumber(res, "nspname");
	i_extrelocatable = PQfnumber(res, "extrelocatable");
	i_extversion = PQfnumber(res, "extversion");
	i_extconfig = PQfnumber(res, "extconfig");
	i_extcondition = PQfnumber(res, "extcondition");

	for (i = 0; i < ntups; i++)
	{
		extinfo[i].dobj.objType = DO_EXTENSION;
		extinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		extinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&extinfo[i].dobj);
		extinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_extname));
		extinfo[i].namespace = pg_strdup(PQgetvalue(res, i, i_nspname));
		extinfo[i].relocatable = *(PQgetvalue(res, i, i_extrelocatable)) == 't';
		extinfo[i].extversion = pg_strdup(PQgetvalue(res, i, i_extversion));
		extinfo[i].extconfig = pg_strdup(PQgetvalue(res, i, i_extconfig));
		extinfo[i].extcondition = pg_strdup(PQgetvalue(res, i, i_extcondition));

		/* Decide whether we want to dump it */
		selectDumpableExtension(&(extinfo[i]), dopt);
	}

	PQclear(res);
	destroyPQExpBuffer(query);

	*numExtensions = ntups;

	return extinfo;
}

/*
 * getTypes:
 *	  read all types in the system catalogs and return them in the
 * TypeInfo* structure
 *
 *	numTypes is set to the number of types read in
 *
 * NB: this must run after getFuncs() because we assume we can do
 * findFuncByOid().
 */
TypeInfo *
getTypes(Archive *fout, int *numTypes)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query = createPQExpBuffer();
	TypeInfo   *tyinfo;
	ShellTypeInfo *stinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_typname;
	int			i_typnamespace;
	int			i_typacl;
	int			i_acldefault;
	int			i_typowner;
	int			i_typelem;
	int			i_typrelid;
	int			i_typrelkind;
	int			i_typtype;
	int			i_typisdefined;
	int			i_isarray;

	/*
	 * we include even the built-in types because those may be used as array
	 * elements by user-defined types
	 *
	 * we filter out the built-in types when we dump out the types
	 *
	 * same approach for undefined (shell) types and array types
	 *
	 * Note: as of 8.3 we can reliably detect whether a type is an
	 * auto-generated array type by checking the element type's typarray.
	 * (Before that the test is capable of generating false positives.) We
	 * still check for name beginning with '_', though, so as to avoid the
	 * cost of the subselect probe for all standard types.  This would have to
	 * be revisited if the backend ever allows renaming of array types.
	 */
	appendPQExpBufferStr(query, "SELECT tableoid, oid, typname, "
						 "typnamespace, typacl, "
						 "acldefault('T', typowner) AS acldefault, "
						 "typowner, "
						 "typelem, typrelid, "
						 "CASE WHEN typrelid = 0 THEN ' '::\"char\" "
						 "ELSE (SELECT relkind FROM pg_class WHERE oid = typrelid) END AS typrelkind, "
						 "typtype, typisdefined, "
						 "typname[0] = '_' AND typelem != 0 AND "
						 "(SELECT typarray FROM pg_type te WHERE oid = pg_type.typelem) = oid AS isarray "
						 "FROM pg_type");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	tyinfo = (TypeInfo *) pg_malloc(ntups * sizeof(TypeInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_typname = PQfnumber(res, "typname");
	i_typnamespace = PQfnumber(res, "typnamespace");
	i_typacl = PQfnumber(res, "typacl");
	i_acldefault = PQfnumber(res, "acldefault");
	i_typowner = PQfnumber(res, "typowner");
	i_typelem = PQfnumber(res, "typelem");
	i_typrelid = PQfnumber(res, "typrelid");
	i_typrelkind = PQfnumber(res, "typrelkind");
	i_typtype = PQfnumber(res, "typtype");
	i_typisdefined = PQfnumber(res, "typisdefined");
	i_isarray = PQfnumber(res, "isarray");

	for (i = 0; i < ntups; i++)
	{
		tyinfo[i].dobj.objType = DO_TYPE;
		tyinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		tyinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&tyinfo[i].dobj);
		tyinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_typname));
		tyinfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_typnamespace)));
		tyinfo[i].dacl.acl = pg_strdup(PQgetvalue(res, i, i_typacl));
		tyinfo[i].dacl.acldefault = pg_strdup(PQgetvalue(res, i, i_acldefault));
		tyinfo[i].dacl.privtype = 0;
		tyinfo[i].dacl.initprivs = NULL;
		tyinfo[i].ftypname = NULL;	/* may get filled later */
		tyinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_typowner));
		tyinfo[i].typelem = atooid(PQgetvalue(res, i, i_typelem));
		tyinfo[i].typrelid = atooid(PQgetvalue(res, i, i_typrelid));
		tyinfo[i].typrelkind = *PQgetvalue(res, i, i_typrelkind);
		tyinfo[i].typtype = *PQgetvalue(res, i, i_typtype);
		tyinfo[i].shellType = NULL;

		if (strcmp(PQgetvalue(res, i, i_typisdefined), "t") == 0)
			tyinfo[i].isDefined = true;
		else
			tyinfo[i].isDefined = false;

		if (strcmp(PQgetvalue(res, i, i_isarray), "t") == 0)
			tyinfo[i].isArray = true;
		else
			tyinfo[i].isArray = false;

		if (tyinfo[i].typtype == TYPTYPE_MULTIRANGE)
			tyinfo[i].isMultirange = true;
		else
			tyinfo[i].isMultirange = false;

		/* Decide whether we want to dump it */
		selectDumpableType(&tyinfo[i], fout);

		/* Mark whether type has an ACL */
		if (!PQgetisnull(res, i, i_typacl))
			tyinfo[i].dobj.components |= DUMP_COMPONENT_ACL;

		/*
		 * If it's a domain, fetch info about its constraints, if any
		 */
		tyinfo[i].nDomChecks = 0;
		tyinfo[i].domChecks = NULL;
		if ((tyinfo[i].dobj.dump & DUMP_COMPONENT_DEFINITION) &&
			tyinfo[i].typtype == TYPTYPE_DOMAIN)
			getDomainConstraints(fout, &(tyinfo[i]));

		/*
		 * If it's a base type, make a DumpableObject representing a shell
		 * definition of the type.  We will need to dump that ahead of the I/O
		 * functions for the type.  Similarly, range types need a shell
		 * definition in case they have a canonicalize function.
		 *
		 * Note: the shell type doesn't have a catId.  You might think it
		 * should copy the base type's catId, but then it might capture the
		 * pg_depend entries for the type, which we don't want.
		 */
		if ((tyinfo[i].dobj.dump & DUMP_COMPONENT_DEFINITION) &&
			(tyinfo[i].typtype == TYPTYPE_BASE ||
			 tyinfo[i].typtype == TYPTYPE_RANGE))
		{
			stinfo = (ShellTypeInfo *) pg_malloc(sizeof(ShellTypeInfo));
			stinfo->dobj.objType = DO_SHELL_TYPE;
			stinfo->dobj.catId = nilCatalogId;
			AssignDumpId(&stinfo->dobj);
			stinfo->dobj.name = pg_strdup(tyinfo[i].dobj.name);
			stinfo->dobj.namespace = tyinfo[i].dobj.namespace;
			stinfo->baseType = &(tyinfo[i]);
			tyinfo[i].shellType = stinfo;

			/*
			 * Initially mark the shell type as not to be dumped.  We'll only
			 * dump it if the I/O or canonicalize functions need to be dumped;
			 * this is taken care of while sorting dependencies.
			 */
			stinfo->dobj.dump = DUMP_COMPONENT_NONE;
		}
	}

	*numTypes = ntups;

	PQclear(res);

	destroyPQExpBuffer(query);

	return tyinfo;
}

/*
 * getOperators:
 *	  read all operators in the system catalogs and return them in the
 * OprInfo* structure
 *
 *	numOprs is set to the number of operators read in
 */
OprInfo *
getOperators(Archive *fout, int *numOprs)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query = createPQExpBuffer();
	OprInfo    *oprinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_oprname;
	int			i_oprnamespace;
	int			i_oprowner;
	int			i_oprkind;
	int			i_oprcode;

	/*
	 * find all operators, including builtin operators; we filter out
	 * system-defined operators at dump-out time.
	 */

	appendPQExpBufferStr(query, "SELECT tableoid, oid, oprname, "
						 "oprnamespace, "
						 "oprowner, "
						 "oprkind, "
						 "oprcode::oid AS oprcode "
						 "FROM pg_operator");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numOprs = ntups;

	oprinfo = (OprInfo *) pg_malloc(ntups * sizeof(OprInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_oprname = PQfnumber(res, "oprname");
	i_oprnamespace = PQfnumber(res, "oprnamespace");
	i_oprowner = PQfnumber(res, "oprowner");
	i_oprkind = PQfnumber(res, "oprkind");
	i_oprcode = PQfnumber(res, "oprcode");

	for (i = 0; i < ntups; i++)
	{
		oprinfo[i].dobj.objType = DO_OPERATOR;
		oprinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		oprinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&oprinfo[i].dobj);
		oprinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_oprname));
		oprinfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_oprnamespace)));
		oprinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_oprowner));
		oprinfo[i].oprkind = (PQgetvalue(res, i, i_oprkind))[0];
		oprinfo[i].oprcode = atooid(PQgetvalue(res, i, i_oprcode));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(oprinfo[i].dobj), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return oprinfo;
}

/*
 * getCollations:
 *	  read all collations in the system catalogs and return them in the
 * CollInfo* structure
 *
 *	numCollations is set to the number of collations read in
 */
CollInfo *
getCollations(Archive *fout, int *numCollations)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	CollInfo   *collinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_collname;
	int			i_collnamespace;
	int			i_collowner;

	query = createPQExpBuffer();

	/*
	 * find all collations, including builtin collations; we filter out
	 * system-defined collations at dump-out time.
	 */

	appendPQExpBufferStr(query, "SELECT tableoid, oid, collname, "
						 "collnamespace, "
						 "collowner "
						 "FROM pg_collation");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numCollations = ntups;

	collinfo = (CollInfo *) pg_malloc(ntups * sizeof(CollInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_collname = PQfnumber(res, "collname");
	i_collnamespace = PQfnumber(res, "collnamespace");
	i_collowner = PQfnumber(res, "collowner");

	for (i = 0; i < ntups; i++)
	{
		collinfo[i].dobj.objType = DO_COLLATION;
		collinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		collinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&collinfo[i].dobj);
		collinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_collname));
		collinfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_collnamespace)));
		collinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_collowner));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(collinfo[i].dobj), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return collinfo;
}

/*
 * getConversions:
 *	  read all conversions in the system catalogs and return them in the
 * ConvInfo* structure
 *
 *	numConversions is set to the number of conversions read in
 */
ConvInfo *
getConversions(Archive *fout, int *numConversions)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	ConvInfo   *convinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_conname;
	int			i_connamespace;
	int			i_conowner;

	query = createPQExpBuffer();

	/*
	 * find all conversions, including builtin conversions; we filter out
	 * system-defined conversions at dump-out time.
	 */

	appendPQExpBufferStr(query, "SELECT tableoid, oid, conname, "
						 "connamespace, "
						 "conowner "
						 "FROM pg_conversion");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numConversions = ntups;

	convinfo = (ConvInfo *) pg_malloc(ntups * sizeof(ConvInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_conname = PQfnumber(res, "conname");
	i_connamespace = PQfnumber(res, "connamespace");
	i_conowner = PQfnumber(res, "conowner");

	for (i = 0; i < ntups; i++)
	{
		convinfo[i].dobj.objType = DO_CONVERSION;
		convinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		convinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&convinfo[i].dobj);
		convinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_conname));
		convinfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_connamespace)));
		convinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_conowner));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(convinfo[i].dobj), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return convinfo;
}

/*
 * getAccessMethods:
 *	  read all user-defined access methods in the system catalogs and return
 *	  them in the AccessMethodInfo* structure
 *
 *	numAccessMethods is set to the number of access methods read in
 */
AccessMethodInfo *
getAccessMethods(Archive *fout, int *numAccessMethods)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	AccessMethodInfo *aminfo;
	int			i_tableoid;
	int			i_oid;
	int			i_amname;
	int			i_amhandler;
	int			i_amtype;

	/* Before 9.6, there are no user-defined access methods */
	if (fout->remoteVersion < 90600)
	{
		*numAccessMethods = 0;
		return NULL;
	}

	query = createPQExpBuffer();

	/* Select all access methods from pg_am table */
	appendPQExpBufferStr(query, "SELECT tableoid, oid, amname, amtype, "
						 "amhandler::pg_catalog.regproc AS amhandler "
						 "FROM pg_am");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numAccessMethods = ntups;

	aminfo = (AccessMethodInfo *) pg_malloc(ntups * sizeof(AccessMethodInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_amname = PQfnumber(res, "amname");
	i_amhandler = PQfnumber(res, "amhandler");
	i_amtype = PQfnumber(res, "amtype");

	for (i = 0; i < ntups; i++)
	{
		aminfo[i].dobj.objType = DO_ACCESS_METHOD;
		aminfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		aminfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&aminfo[i].dobj);
		aminfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_amname));
		aminfo[i].dobj.namespace = NULL;
		aminfo[i].amhandler = pg_strdup(PQgetvalue(res, i, i_amhandler));
		aminfo[i].amtype = *(PQgetvalue(res, i, i_amtype));

		/* Decide whether we want to dump it */
		selectDumpableAccessMethod(&(aminfo[i]), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return aminfo;
}


/*
 * getOpclasses:
 *	  read all opclasses in the system catalogs and return them in the
 * OpclassInfo* structure
 *
 *	numOpclasses is set to the number of opclasses read in
 */
OpclassInfo *
getOpclasses(Archive *fout, int *numOpclasses)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query = createPQExpBuffer();
	OpclassInfo *opcinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_opcname;
	int			i_opcnamespace;
	int			i_opcowner;

	/*
	 * find all opclasses, including builtin opclasses; we filter out
	 * system-defined opclasses at dump-out time.
	 */

	appendPQExpBufferStr(query, "SELECT tableoid, oid, opcname, "
						 "opcnamespace, "
						 "opcowner "
						 "FROM pg_opclass");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numOpclasses = ntups;

	opcinfo = (OpclassInfo *) pg_malloc(ntups * sizeof(OpclassInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_opcname = PQfnumber(res, "opcname");
	i_opcnamespace = PQfnumber(res, "opcnamespace");
	i_opcowner = PQfnumber(res, "opcowner");

	for (i = 0; i < ntups; i++)
	{
		opcinfo[i].dobj.objType = DO_OPCLASS;
		opcinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		opcinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&opcinfo[i].dobj);
		opcinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_opcname));
		opcinfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_opcnamespace)));
		opcinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_opcowner));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(opcinfo[i].dobj), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return opcinfo;
}

/*
 * getOpfamilies:
 *	  read all opfamilies in the system catalogs and return them in the
 * OpfamilyInfo* structure
 *
 *	numOpfamilies is set to the number of opfamilies read in
 */
OpfamilyInfo *
getOpfamilies(Archive *fout, int *numOpfamilies)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	OpfamilyInfo *opfinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_opfname;
	int			i_opfnamespace;
	int			i_opfowner;

	query = createPQExpBuffer();

	/*
	 * find all opfamilies, including builtin opfamilies; we filter out
	 * system-defined opfamilies at dump-out time.
	 */

	appendPQExpBufferStr(query, "SELECT tableoid, oid, opfname, "
						 "opfnamespace, "
						 "opfowner "
						 "FROM pg_opfamily");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numOpfamilies = ntups;

	opfinfo = (OpfamilyInfo *) pg_malloc(ntups * sizeof(OpfamilyInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_opfname = PQfnumber(res, "opfname");
	i_opfnamespace = PQfnumber(res, "opfnamespace");
	i_opfowner = PQfnumber(res, "opfowner");

	for (i = 0; i < ntups; i++)
	{
		opfinfo[i].dobj.objType = DO_OPFAMILY;
		opfinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		opfinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&opfinfo[i].dobj);
		opfinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_opfname));
		opfinfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_opfnamespace)));
		opfinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_opfowner));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(opfinfo[i].dobj), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return opfinfo;
}

/*
 * getAggregates:
 *	  read all the user-defined aggregates in the system catalogs and
 * return them in the AggInfo* structure
 *
 * numAggs is set to the number of aggregates read in
 */
AggInfo *
getAggregates(Archive *fout, int *numAggs)
{
	DumpOptions *dopt = fout->dopt;
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query = createPQExpBuffer();
	AggInfo    *agginfo;
	int			i_tableoid;
	int			i_oid;
	int			i_aggname;
	int			i_aggnamespace;
	int			i_pronargs;
	int			i_proargtypes;
	int			i_proowner;
	int			i_aggacl;
	int			i_acldefault;

	/*
	 * Find all interesting aggregates.  See comment in getFuncs() for the
	 * rationale behind the filtering logic.
	 */
	if (fout->remoteVersion >= 90600)
	{
		const char *agg_check;

		agg_check = (fout->remoteVersion >= 110000 ? "p.prokind = 'a'"
					 : "p.proisagg");

		appendPQExpBuffer(query, "SELECT p.tableoid, p.oid, "
						  "p.proname AS aggname, "
						  "p.pronamespace AS aggnamespace, "
						  "p.pronargs, p.proargtypes, "
						  "p.proowner, "
						  "p.proacl AS aggacl, "
						  "acldefault('f', p.proowner) AS acldefault "
						  "FROM pg_proc p "
						  "LEFT JOIN pg_init_privs pip ON "
						  "(p.oid = pip.objoid "
						  "AND pip.classoid = 'pg_proc'::regclass "
						  "AND pip.objsubid = 0) "
						  "WHERE %s AND ("
						  "p.pronamespace != "
						  "(SELECT oid FROM pg_namespace "
						  "WHERE nspname = 'pg_catalog') OR "
						  "p.proacl IS DISTINCT FROM pip.initprivs",
						  agg_check);
		if (dopt->binary_upgrade)
			appendPQExpBufferStr(query,
								 " OR EXISTS(SELECT 1 FROM pg_depend WHERE "
								 "classid = 'pg_proc'::regclass AND "
								 "objid = p.oid AND "
								 "refclassid = 'pg_extension'::regclass AND "
								 "deptype = 'e')");
		appendPQExpBufferChar(query, ')');
	}
	else
	{
		appendPQExpBufferStr(query, "SELECT tableoid, oid, proname AS aggname, "
							 "pronamespace AS aggnamespace, "
							 "pronargs, proargtypes, "
							 "proowner, "
							 "proacl AS aggacl, "
							 "acldefault('f', proowner) AS acldefault "
							 "FROM pg_proc p "
							 "WHERE proisagg AND ("
							 "pronamespace != "
							 "(SELECT oid FROM pg_namespace "
							 "WHERE nspname = 'pg_catalog')");
		if (dopt->binary_upgrade)
			appendPQExpBufferStr(query,
								 " OR EXISTS(SELECT 1 FROM pg_depend WHERE "
								 "classid = 'pg_proc'::regclass AND "
								 "objid = p.oid AND "
								 "refclassid = 'pg_extension'::regclass AND "
								 "deptype = 'e')");
		appendPQExpBufferChar(query, ')');
	}

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numAggs = ntups;

	agginfo = (AggInfo *) pg_malloc(ntups * sizeof(AggInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_aggname = PQfnumber(res, "aggname");
	i_aggnamespace = PQfnumber(res, "aggnamespace");
	i_pronargs = PQfnumber(res, "pronargs");
	i_proargtypes = PQfnumber(res, "proargtypes");
	i_proowner = PQfnumber(res, "proowner");
	i_aggacl = PQfnumber(res, "aggacl");
	i_acldefault = PQfnumber(res, "acldefault");

	for (i = 0; i < ntups; i++)
	{
		agginfo[i].aggfn.dobj.objType = DO_AGG;
		agginfo[i].aggfn.dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		agginfo[i].aggfn.dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&agginfo[i].aggfn.dobj);
		agginfo[i].aggfn.dobj.name = pg_strdup(PQgetvalue(res, i, i_aggname));
		agginfo[i].aggfn.dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_aggnamespace)));
		agginfo[i].aggfn.dacl.acl = pg_strdup(PQgetvalue(res, i, i_aggacl));
		agginfo[i].aggfn.dacl.acldefault = pg_strdup(PQgetvalue(res, i, i_acldefault));
		agginfo[i].aggfn.dacl.privtype = 0;
		agginfo[i].aggfn.dacl.initprivs = NULL;
		agginfo[i].aggfn.rolname = getRoleName(PQgetvalue(res, i, i_proowner));
		agginfo[i].aggfn.lang = InvalidOid; /* not currently interesting */
		agginfo[i].aggfn.prorettype = InvalidOid;	/* not saved */
		agginfo[i].aggfn.nargs = atoi(PQgetvalue(res, i, i_pronargs));
		if (agginfo[i].aggfn.nargs == 0)
			agginfo[i].aggfn.argtypes = NULL;
		else
		{
			agginfo[i].aggfn.argtypes = (Oid *) pg_malloc(agginfo[i].aggfn.nargs * sizeof(Oid));
			parseOidArray(PQgetvalue(res, i, i_proargtypes),
						  agginfo[i].aggfn.argtypes,
						  agginfo[i].aggfn.nargs);
		}
		agginfo[i].aggfn.postponed_def = false; /* might get set during sort */

		/* Decide whether we want to dump it */
		selectDumpableObject(&(agginfo[i].aggfn.dobj), fout);

		/* Mark whether aggregate has an ACL */
		if (!PQgetisnull(res, i, i_aggacl))
			agginfo[i].aggfn.dobj.components |= DUMP_COMPONENT_ACL;
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return agginfo;
}

/*
 * getFuncs:
 *	  read all the user-defined functions in the system catalogs and
 * return them in the FuncInfo* structure
 *
 * numFuncs is set to the number of functions read in
 */
FuncInfo *
getFuncs(Archive *fout, int *numFuncs)
{
	DumpOptions *dopt = fout->dopt;
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query = createPQExpBuffer();
	FuncInfo   *finfo;
	int			i_tableoid;
	int			i_oid;
	int			i_proname;
	int			i_pronamespace;
	int			i_proowner;
	int			i_prolang;
	int			i_pronargs;
	int			i_proargtypes;
	int			i_prorettype;
	int			i_proacl;
	int			i_acldefault;

	/*
	 * Find all interesting functions.  This is a bit complicated:
	 *
	 * 1. Always exclude aggregates; those are handled elsewhere.
	 *
	 * 2. Always exclude functions that are internally dependent on something
	 * else, since presumably those will be created as a result of creating
	 * the something else.  This currently acts only to suppress constructor
	 * functions for range types.  Note this is OK only because the
	 * constructors don't have any dependencies the range type doesn't have;
	 * otherwise we might not get creation ordering correct.
	 *
	 * 3. Otherwise, we normally exclude functions in pg_catalog.  However, if
	 * they're members of extensions and we are in binary-upgrade mode then
	 * include them, since we want to dump extension members individually in
	 * that mode.  Also, if they are used by casts or transforms then we need
	 * to gather the information about them, though they won't be dumped if
	 * they are built-in.  Also, in 9.6 and up, include functions in
	 * pg_catalog if they have an ACL different from what's shown in
	 * pg_init_privs (so we have to join to pg_init_privs; annoying).
	 */
	if (fout->remoteVersion >= 90600)
	{
		const char *not_agg_check;

		not_agg_check = (fout->remoteVersion >= 110000 ? "p.prokind <> 'a'"
						 : "NOT p.proisagg");

		appendPQExpBuffer(query,
						  "SELECT p.tableoid, p.oid, p.proname, p.prolang, "
						  "p.pronargs, p.proargtypes, p.prorettype, "
						  "p.proacl, "
						  "acldefault('f', p.proowner) AS acldefault, "
						  "p.pronamespace, "
						  "p.proowner "
						  "FROM pg_proc p "
						  "LEFT JOIN pg_init_privs pip ON "
						  "(p.oid = pip.objoid "
						  "AND pip.classoid = 'pg_proc'::regclass "
						  "AND pip.objsubid = 0) "
						  "WHERE %s"
						  "\n  AND NOT EXISTS (SELECT 1 FROM pg_depend "
						  "WHERE classid = 'pg_proc'::regclass AND "
						  "objid = p.oid AND deptype = 'i')"
						  "\n  AND ("
						  "\n  pronamespace != "
						  "(SELECT oid FROM pg_namespace "
						  "WHERE nspname = 'pg_catalog')"
						  "\n  OR EXISTS (SELECT 1 FROM pg_cast"
						  "\n  WHERE pg_cast.oid > %u "
						  "\n  AND p.oid = pg_cast.castfunc)"
						  "\n  OR EXISTS (SELECT 1 FROM pg_transform"
						  "\n  WHERE pg_transform.oid > %u AND "
						  "\n  (p.oid = pg_transform.trffromsql"
						  "\n  OR p.oid = pg_transform.trftosql))",
						  not_agg_check,
						  g_last_builtin_oid,
						  g_last_builtin_oid);
		if (dopt->binary_upgrade)
			appendPQExpBufferStr(query,
								 "\n  OR EXISTS(SELECT 1 FROM pg_depend WHERE "
								 "classid = 'pg_proc'::regclass AND "
								 "objid = p.oid AND "
								 "refclassid = 'pg_extension'::regclass AND "
								 "deptype = 'e')");
		appendPQExpBufferStr(query,
							 "\n  OR p.proacl IS DISTINCT FROM pip.initprivs");
		appendPQExpBufferChar(query, ')');
	}
	else
	{
		appendPQExpBuffer(query,
						  "SELECT tableoid, oid, proname, prolang, "
						  "pronargs, proargtypes, prorettype, proacl, "
						  "acldefault('f', proowner) AS acldefault, "
						  "pronamespace, "
						  "proowner "
						  "FROM pg_proc p "
						  "WHERE NOT proisagg"
						  "\n  AND NOT EXISTS (SELECT 1 FROM pg_depend "
						  "WHERE classid = 'pg_proc'::regclass AND "
						  "objid = p.oid AND deptype = 'i')"
						  "\n  AND ("
						  "\n  pronamespace != "
						  "(SELECT oid FROM pg_namespace "
						  "WHERE nspname = 'pg_catalog')"
						  "\n  OR EXISTS (SELECT 1 FROM pg_cast"
						  "\n  WHERE pg_cast.oid > '%u'::oid"
						  "\n  AND p.oid = pg_cast.castfunc)",
						  g_last_builtin_oid);

		if (fout->remoteVersion >= 90500)
			appendPQExpBuffer(query,
							  "\n  OR EXISTS (SELECT 1 FROM pg_transform"
							  "\n  WHERE pg_transform.oid > '%u'::oid"
							  "\n  AND (p.oid = pg_transform.trffromsql"
							  "\n  OR p.oid = pg_transform.trftosql))",
							  g_last_builtin_oid);

		if (dopt->binary_upgrade)
			appendPQExpBufferStr(query,
								 "\n  OR EXISTS(SELECT 1 FROM pg_depend WHERE "
								 "classid = 'pg_proc'::regclass AND "
								 "objid = p.oid AND "
								 "refclassid = 'pg_extension'::regclass AND "
								 "deptype = 'e')");
		appendPQExpBufferChar(query, ')');
	}

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	*numFuncs = ntups;

	finfo = (FuncInfo *) pg_malloc0(ntups * sizeof(FuncInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_proname = PQfnumber(res, "proname");
	i_pronamespace = PQfnumber(res, "pronamespace");
	i_proowner = PQfnumber(res, "proowner");
	i_prolang = PQfnumber(res, "prolang");
	i_pronargs = PQfnumber(res, "pronargs");
	i_proargtypes = PQfnumber(res, "proargtypes");
	i_prorettype = PQfnumber(res, "prorettype");
	i_proacl = PQfnumber(res, "proacl");
	i_acldefault = PQfnumber(res, "acldefault");

	for (i = 0; i < ntups; i++)
	{
		finfo[i].dobj.objType = DO_FUNC;
		finfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		finfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&finfo[i].dobj);
		finfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_proname));
		finfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_pronamespace)));
		finfo[i].dacl.acl = pg_strdup(PQgetvalue(res, i, i_proacl));
		finfo[i].dacl.acldefault = pg_strdup(PQgetvalue(res, i, i_acldefault));
		finfo[i].dacl.privtype = 0;
		finfo[i].dacl.initprivs = NULL;
		finfo[i].rolname = getRoleName(PQgetvalue(res, i, i_proowner));
		finfo[i].lang = atooid(PQgetvalue(res, i, i_prolang));
		finfo[i].prorettype = atooid(PQgetvalue(res, i, i_prorettype));
		finfo[i].nargs = atoi(PQgetvalue(res, i, i_pronargs));
		if (finfo[i].nargs == 0)
			finfo[i].argtypes = NULL;
		else
		{
			finfo[i].argtypes = (Oid *) pg_malloc(finfo[i].nargs * sizeof(Oid));
			parseOidArray(PQgetvalue(res, i, i_proargtypes),
						  finfo[i].argtypes, finfo[i].nargs);
		}
		finfo[i].postponed_def = false; /* might get set during sort */

		/* Decide whether we want to dump it */
		selectDumpableObject(&(finfo[i].dobj), fout);

		/* Mark whether function has an ACL */
		if (!PQgetisnull(res, i, i_proacl))
			finfo[i].dobj.components |= DUMP_COMPONENT_ACL;
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return finfo;
}

/*
 * getTables
 *	  read all the tables (no indexes) in the system catalogs,
 *	  and return them as an array of TableInfo structures
 *
 * *numTables is set to the number of tables read in
 */
TableInfo *
getTables(Archive *fout, int *numTables)
{
	DumpOptions *dopt = fout->dopt;
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query = createPQExpBuffer();
	TableInfo  *tblinfo;
	int			i_reltableoid;
	int			i_reloid;
	int			i_relname;
	int			i_relnamespace;
	int			i_relkind;
	int			i_reltype;
	int			i_relowner;
	int			i_relchecks;
	int			i_relhasindex;
	int			i_relhasrules;
	int			i_relpages;
	int			i_toastpages;
	int			i_owning_tab;
	int			i_owning_col;
	int			i_reltablespace;
	int			i_relhasoids;
	int			i_relhastriggers;
	int			i_relpersistence;
	int			i_relispopulated;
	int			i_relreplident;
	int			i_relrowsec;
	int			i_relforcerowsec;
	int			i_relfrozenxid;
	int			i_toastfrozenxid;
	int			i_toastoid;
	int			i_relminmxid;
	int			i_toastminmxid;
	int			i_reloptions;
	int			i_checkoption;
	int			i_toastreloptions;
	int			i_reloftype;
	int			i_foreignserver;
	int			i_amname;
	int			i_is_identity_sequence;
	int			i_relacl;
	int			i_acldefault;
	int			i_ispartition;

	/*
	 * Find all the tables and table-like objects.
	 *
	 * We must fetch all tables in this phase because otherwise we cannot
	 * correctly identify inherited columns, owned sequences, etc.
	 *
	 * We include system catalogs, so that we can work if a user table is
	 * defined to inherit from a system catalog (pretty weird, but...)
	 *
	 * Note: in this phase we should collect only a minimal amount of
	 * information about each table, basically just enough to decide if it is
	 * interesting.  In particular, since we do not yet have lock on any user
	 * table, we MUST NOT invoke any server-side data collection functions
	 * (for instance, pg_get_partkeydef()).  Those are likely to fail or give
	 * wrong answers if any concurrent DDL is happening.
	 */

	appendPQExpBufferStr(query,
						 "SELECT c.tableoid, c.oid, c.relname, "
						 "c.relnamespace, c.relkind, c.reltype, "
						 "c.relowner, "
						 "c.relchecks, "
						 "c.relhasindex, c.relhasrules, c.relpages, "
						 "c.relhastriggers, "
						 "c.relpersistence, "
						 "c.reloftype, "
						 "c.relacl, "
						 "acldefault(CASE WHEN c.relkind = " CppAsString2(RELKIND_SEQUENCE)
						 " THEN 's'::\"char\" ELSE 'r'::\"char\" END, c.relowner) AS acldefault, "
						 "CASE WHEN c.relkind = " CppAsString2(RELKIND_FOREIGN_TABLE) " THEN "
						 "(SELECT ftserver FROM pg_catalog.pg_foreign_table WHERE ftrelid = c.oid) "
						 "ELSE 0 END AS foreignserver, "
						 "c.relfrozenxid, tc.relfrozenxid AS tfrozenxid, "
						 "tc.oid AS toid, "
						 "tc.relpages AS toastpages, "
						 "tc.reloptions AS toast_reloptions, "
						 "d.refobjid AS owning_tab, "
						 "d.refobjsubid AS owning_col, "
						 "tsp.spcname AS reltablespace, ");

	if (fout->remoteVersion >= 120000)
		appendPQExpBufferStr(query,
							 "false AS relhasoids, ");
	else
		appendPQExpBufferStr(query,
							 "c.relhasoids, ");

	if (fout->remoteVersion >= 90300)
		appendPQExpBufferStr(query,
							 "c.relispopulated, ");
	else
		appendPQExpBufferStr(query,
							 "'t' as relispopulated, ");

	if (fout->remoteVersion >= 90400)
		appendPQExpBufferStr(query,
							 "c.relreplident, ");
	else
		appendPQExpBufferStr(query,
							 "'d' AS relreplident, ");

	if (fout->remoteVersion >= 90500)
		appendPQExpBufferStr(query,
							 "c.relrowsecurity, c.relforcerowsecurity, ");
	else
		appendPQExpBufferStr(query,
							 "false AS relrowsecurity, "
							 "false AS relforcerowsecurity, ");

	if (fout->remoteVersion >= 90300)
		appendPQExpBufferStr(query,
							 "c.relminmxid, tc.relminmxid AS tminmxid, ");
	else
		appendPQExpBufferStr(query,
							 "0 AS relminmxid, 0 AS tminmxid, ");

	if (fout->remoteVersion >= 90300)
		appendPQExpBufferStr(query,
							 "array_remove(array_remove(c.reloptions,'check_option=local'),'check_option=cascaded') AS reloptions, "
							 "CASE WHEN 'check_option=local' = ANY (c.reloptions) THEN 'LOCAL'::text "
							 "WHEN 'check_option=cascaded' = ANY (c.reloptions) THEN 'CASCADED'::text ELSE NULL END AS checkoption, ");
	else
		appendPQExpBufferStr(query,
							 "c.reloptions, NULL AS checkoption, ");

	if (fout->remoteVersion >= 90600)
		appendPQExpBufferStr(query,
							 "am.amname, ");
	else
		appendPQExpBufferStr(query,
							 "NULL AS amname, ");

	if (fout->remoteVersion >= 90600)
		appendPQExpBufferStr(query,
							 "(d.deptype = 'i') IS TRUE AS is_identity_sequence, ");
	else
		appendPQExpBufferStr(query,
							 "false AS is_identity_sequence, ");

	if (fout->remoteVersion >= 100000)
		appendPQExpBufferStr(query,
							 "c.relispartition AS ispartition ");
	else
		appendPQExpBufferStr(query,
							 "false AS ispartition ");

	/*
	 * Left join to pg_depend to pick up dependency info linking sequences to
	 * their owning column, if any (note this dependency is AUTO except for
	 * identity sequences, where it's INTERNAL). Also join to pg_tablespace to
	 * collect the spcname.
	 */
	appendPQExpBufferStr(query,
						 "\nFROM pg_class c\n"
						 "LEFT JOIN pg_depend d ON "
						 "(c.relkind = " CppAsString2(RELKIND_SEQUENCE) " AND "
						 "d.classid = 'pg_class'::regclass AND d.objid = c.oid AND "
						 "d.objsubid = 0 AND "
						 "d.refclassid = 'pg_class'::regclass AND d.deptype IN ('a', 'i'))\n"
						 "LEFT JOIN pg_tablespace tsp ON (tsp.oid = c.reltablespace)\n");

	/*
	 * In 9.6 and up, left join to pg_am to pick up the amname.
	 */
	if (fout->remoteVersion >= 90600)
		appendPQExpBufferStr(query,
							 "LEFT JOIN pg_am am ON (c.relam = am.oid)\n");

	/*
	 * We purposefully ignore toast OIDs for partitioned tables; the reason is
	 * that versions 10 and 11 have them, but later versions do not, so
	 * emitting them causes the upgrade to fail.
	 */
	appendPQExpBufferStr(query,
						 "LEFT JOIN pg_class tc ON (c.reltoastrelid = tc.oid"
						 " AND tc.relkind = " CppAsString2(RELKIND_TOASTVALUE)
						 " AND c.relkind <> " CppAsString2(RELKIND_PARTITIONED_TABLE) ")\n");

	/*
	 * Restrict to interesting relkinds (in particular, not indexes).  Not all
	 * relkinds are possible in older servers, but it's not worth the trouble
	 * to emit a version-dependent list.
	 *
	 * Composite-type table entries won't be dumped as such, but we have to
	 * make a DumpableObject for them so that we can track dependencies of the
	 * composite type (pg_depend entries for columns of the composite type
	 * link to the pg_class entry not the pg_type entry).
	 */
	appendPQExpBufferStr(query,
						 "WHERE c.relkind IN ("
						 CppAsString2(RELKIND_RELATION) ", "
						 CppAsString2(RELKIND_SEQUENCE) ", "
						 CppAsString2(RELKIND_VIEW) ", "
						 CppAsString2(RELKIND_COMPOSITE_TYPE) ", "
						 CppAsString2(RELKIND_MATVIEW) ", "
						 CppAsString2(RELKIND_FOREIGN_TABLE) ", "
						 CppAsString2(RELKIND_PARTITIONED_TABLE) ")\n"
						 "ORDER BY c.oid");

	//printf("\n%s\n", query->data);
	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	*numTables = ntups;

	/*
	 * Extract data from result and lock dumpable tables.  We do the locking
	 * before anything else, to minimize the window wherein a table could
	 * disappear under us.
	 *
	 * Note that we have to save info about all tables here, even when dumping
	 * only one, because we don't yet know which tables might be inheritance
	 * ancestors of the target table.
	 */
	
	tblinfo = (TableInfo *) pg_malloc0(ntups * sizeof(TableInfo));
	
	i_reltableoid = PQfnumber(res, "tableoid");
	
	i_reloid = PQfnumber(res, "oid");
	i_relname = PQfnumber(res, "relname");
	i_relnamespace = PQfnumber(res, "relnamespace");
	i_relkind = PQfnumber(res, "relkind");
	i_reltype = PQfnumber(res, "reltype");
	i_relowner = PQfnumber(res, "relowner");
	i_relchecks = PQfnumber(res, "relchecks");
	i_relhasindex = PQfnumber(res, "relhasindex");
	i_relhasrules = PQfnumber(res, "relhasrules");
	i_relpages = PQfnumber(res, "relpages");
	i_toastpages = PQfnumber(res, "toastpages");
	i_owning_tab = PQfnumber(res, "owning_tab");
	i_owning_col = PQfnumber(res, "owning_col");
	i_reltablespace = PQfnumber(res, "reltablespace");
	i_relhasoids = PQfnumber(res, "relhasoids");
	i_relhastriggers = PQfnumber(res, "relhastriggers");
	i_relpersistence = PQfnumber(res, "relpersistence");
	i_relispopulated = PQfnumber(res, "relispopulated");
	i_relreplident = PQfnumber(res, "relreplident");
	i_relrowsec = PQfnumber(res, "relrowsecurity");
	i_relforcerowsec = PQfnumber(res, "relforcerowsecurity");
	i_relfrozenxid = PQfnumber(res, "relfrozenxid");
	i_toastfrozenxid = PQfnumber(res, "tfrozenxid");
	i_toastoid = PQfnumber(res, "toid");
	i_relminmxid = PQfnumber(res, "relminmxid");
	i_toastminmxid = PQfnumber(res, "tminmxid");
	i_reloptions = PQfnumber(res, "reloptions");
	i_checkoption = PQfnumber(res, "checkoption");
	i_toastreloptions = PQfnumber(res, "toast_reloptions");
	i_reloftype = PQfnumber(res, "reloftype");
	i_foreignserver = PQfnumber(res, "foreignserver");
	i_amname = PQfnumber(res, "amname");
	i_is_identity_sequence = PQfnumber(res, "is_identity_sequence");
	i_relacl = PQfnumber(res, "relacl");
	i_acldefault = PQfnumber(res, "acldefault");
	i_ispartition = PQfnumber(res, "ispartition");
	if (dopt->lockWaitTimeout)
	{
		/*
		 * Arrange to fail instead of waiting forever for a table lock.
		 *
		 * NB: this coding assumes that the only queries issued within the
		 * following loop are LOCK TABLEs; else the timeout may be undesirably
		 * applied to other things too.
		 */
		resetPQExpBuffer(query);
		appendPQExpBufferStr(query, "SET statement_timeout = ");
		appendStringLiteralConn(query, dopt->lockWaitTimeout, GetConnection(fout));
		ExecuteSqlStatement(fout, query->data);
	}
	
	resetPQExpBuffer(query);
	
	for (i = 0; i < ntups; i++)
	{
		tblinfo[i].dobj.objType = DO_TABLE;
		tblinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_reltableoid));
		tblinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_reloid));
		AssignDumpId(&tblinfo[i].dobj);
		tblinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_relname));
		tblinfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_relnamespace)));
		tblinfo[i].dacl.acl = pg_strdup(PQgetvalue(res, i, i_relacl));
		tblinfo[i].dacl.acldefault = pg_strdup(PQgetvalue(res, i, i_acldefault));
		tblinfo[i].dacl.privtype = 0;
		tblinfo[i].dacl.initprivs = NULL;
		tblinfo[i].relkind = *(PQgetvalue(res, i, i_relkind));
		tblinfo[i].reltype = atooid(PQgetvalue(res, i, i_reltype));
		tblinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_relowner));
		tblinfo[i].ncheck = atoi(PQgetvalue(res, i, i_relchecks));
		tblinfo[i].hasindex = (strcmp(PQgetvalue(res, i, i_relhasindex), "t") == 0);
		tblinfo[i].hasrules = (strcmp(PQgetvalue(res, i, i_relhasrules), "t") == 0);
		tblinfo[i].relpages = atoi(PQgetvalue(res, i, i_relpages));
		if (PQgetisnull(res, i, i_toastpages))
			tblinfo[i].toastpages = 0;
		else
			tblinfo[i].toastpages = atoi(PQgetvalue(res, i, i_toastpages));
		if (PQgetisnull(res, i, i_owning_tab))
		{
			tblinfo[i].owning_tab = InvalidOid;
			tblinfo[i].owning_col = 0;
		}
		else
		{
			tblinfo[i].owning_tab = atooid(PQgetvalue(res, i, i_owning_tab));
			tblinfo[i].owning_col = atoi(PQgetvalue(res, i, i_owning_col));
		}
		tblinfo[i].reltablespace = pg_strdup(PQgetvalue(res, i, i_reltablespace));
		tblinfo[i].hasoids = (strcmp(PQgetvalue(res, i, i_relhasoids), "t") == 0);
		tblinfo[i].hastriggers = (strcmp(PQgetvalue(res, i, i_relhastriggers), "t") == 0);
		tblinfo[i].relpersistence = *(PQgetvalue(res, i, i_relpersistence));
		tblinfo[i].relispopulated = (strcmp(PQgetvalue(res, i, i_relispopulated), "t") == 0);
		tblinfo[i].relreplident = *(PQgetvalue(res, i, i_relreplident));
		tblinfo[i].rowsec = (strcmp(PQgetvalue(res, i, i_relrowsec), "t") == 0);
		tblinfo[i].forcerowsec = (strcmp(PQgetvalue(res, i, i_relforcerowsec), "t") == 0);
		tblinfo[i].frozenxid = atooid(PQgetvalue(res, i, i_relfrozenxid));
		tblinfo[i].toast_frozenxid = atooid(PQgetvalue(res, i, i_toastfrozenxid));
		tblinfo[i].toast_oid = atooid(PQgetvalue(res, i, i_toastoid));
		tblinfo[i].minmxid = atooid(PQgetvalue(res, i, i_relminmxid));
		tblinfo[i].toast_minmxid = atooid(PQgetvalue(res, i, i_toastminmxid));
		tblinfo[i].reloptions = pg_strdup(PQgetvalue(res, i, i_reloptions));
		if (PQgetisnull(res, i, i_checkoption))
			tblinfo[i].checkoption = NULL;
		else
			tblinfo[i].checkoption = pg_strdup(PQgetvalue(res, i, i_checkoption));
		tblinfo[i].toast_reloptions = pg_strdup(PQgetvalue(res, i, i_toastreloptions));
		tblinfo[i].reloftype = atooid(PQgetvalue(res, i, i_reloftype));
		tblinfo[i].foreign_server = atooid(PQgetvalue(res, i, i_foreignserver));
		if (PQgetisnull(res, i, i_amname))
			tblinfo[i].amname = NULL;
		else
			tblinfo[i].amname = pg_strdup(PQgetvalue(res, i, i_amname));
		tblinfo[i].is_identity_sequence = (strcmp(PQgetvalue(res, i, i_is_identity_sequence), "t") == 0);
		tblinfo[i].ispartition = (strcmp(PQgetvalue(res, i, i_ispartition), "t") == 0);

		/* other fields were zeroed above */

		/*
		 * Decide whether we want to dump this table.
		 */
		if (tblinfo[i].relkind == RELKIND_COMPOSITE_TYPE)
			tblinfo[i].dobj.dump = DUMP_COMPONENT_NONE;
		else {
			selectDumpableTable(&tblinfo[i], fout);
		}
		/*
		 * Now, consider the table "interesting" if we need to dump its
		 * definition or its data.  Later on, we'll skip a lot of data
		 * collection for uninteresting tables.
		 *
		 * Note: the "interesting" flag will also be set by flagInhTables for
		 * parents of interesting tables, so that we collect necessary
		 * inheritance info even when the parents are not themselves being
		 * dumped.  This is the main reason why we need an "interesting" flag
		 * that's separate from the components-to-dump bitmask.
		 */
		tblinfo[i].interesting = (tblinfo[i].dobj.dump &
								  (DUMP_COMPONENT_DEFINITION |
								   DUMP_COMPONENT_DATA)) != 0;

		tblinfo[i].dummy_view = false;	/* might get set during sort */
		tblinfo[i].postponed_def = false;	/* might get set during sort */

		/* Tables have data */
		tblinfo[i].dobj.components |= DUMP_COMPONENT_DATA;

		/* Mark whether table has an ACL */
		if (!PQgetisnull(res, i, i_relacl))
			tblinfo[i].dobj.components |= DUMP_COMPONENT_ACL;
		tblinfo[i].hascolumnACLs = false;	/* may get set later */

		/*
		 * Read-lock target tables to make sure they aren't DROPPED or altered
		 * in schema before we get around to dumping them.
		 *
		 * Note that we don't explicitly lock parents of the target tables; we
		 * assume our lock on the child is enough to prevent schema
		 * alterations to parent tables.
		 *
		 * NOTE: it'd be kinda nice to lock other relations too, not only
		 * plain or partitioned tables, but the backend doesn't presently
		 * allow that.
		 *
		 * We only need to lock the table for certain components; see
		 * pg_dump.h
		 */
		
		if ((tblinfo[i].dobj.dump & DUMP_COMPONENTS_REQUIRING_LOCK) &&
			(tblinfo[i].relkind == RELKIND_RELATION ||
			 tblinfo[i].relkind == RELKIND_PARTITIONED_TABLE))
		{
			/*
			 * Tables are locked in batches.  When dumping from a remote
			 * server this can save a significant amount of time by reducing
			 * the number of round trips.
			 */
			if (query->len == 0)
				appendPQExpBuffer(query, "LOCK TABLE %s",
								  fmtQualifiedDumpable(&tblinfo[i]));
			else
			{
				appendPQExpBuffer(query, ", %s",
								  fmtQualifiedDumpable(&tblinfo[i]));

				/* Arbitrarily end a batch when query length reaches 100K. */
				if (query->len >= 100000)
				{
					/* Lock another batch of tables. */
					appendPQExpBufferStr(query, " IN ACCESS SHARE MODE");
					ExecuteSqlStatement(fout, query->data);
					resetPQExpBuffer(query);
				}
			}
		}
	}
	
	if (query->len != 0)
	{
		/* Lock the tables in the last batch. */
		appendPQExpBufferStr(query, " IN ACCESS SHARE MODE");
		ExecuteSqlStatement(fout, query->data);
	}

	if (dopt->lockWaitTimeout)
	{
		ExecuteSqlStatement(fout, "SET statement_timeout = 0");
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return tblinfo;
}

/*
 * getOwnedSeqs
 *	  identify owned sequences and mark them as dumpable if owning table is
 *
 * We used to do this in getTables(), but it's better to do it after the
 * index used by findTableByOid() has been set up.
 */
void
getOwnedSeqs(Archive *fout, TableInfo tblinfo[], int numTables)
{
	int			i;

	/*
	 * Force sequences that are "owned" by table columns to be dumped whenever
	 * their owning table is being dumped.
	 */
	for (i = 0; i < numTables; i++)
	{
		TableInfo  *seqinfo = &tblinfo[i];
		TableInfo  *owning_tab;

		if (!OidIsValid(seqinfo->owning_tab))
			continue;			/* not an owned sequence */

		owning_tab = findTableByOid(seqinfo->owning_tab);
		if (owning_tab == NULL)
			pg_fatal("failed sanity check, parent table with OID %u of sequence with OID %u not found",
					 seqinfo->owning_tab, seqinfo->dobj.catId.oid);

		/*
		 * For an identity sequence, dump exactly the same components for the
		 * sequence as for the owning table.  This is important because we
		 * treat the identity sequence as an integral part of the table.  For
		 * example, there is not any DDL command that allows creation of such
		 * a sequence independently of the table.
		 *
		 * For other owned sequences such as serial sequences, we need to dump
		 * the components that are being dumped for the table and any
		 * components that the sequence is explicitly marked with.
		 *
		 * We can't simply use the set of components which are being dumped
		 * for the table as the table might be in an extension (and only the
		 * non-extension components, eg: ACLs if changed, security labels, and
		 * policies, are being dumped) while the sequence is not (and
		 * therefore the definition and other components should also be
		 * dumped).
		 *
		 * If the sequence is part of the extension then it should be properly
		 * marked by checkExtensionMembership() and this will be a no-op as
		 * the table will be equivalently marked.
		 */
		if (seqinfo->is_identity_sequence)
			seqinfo->dobj.dump = owning_tab->dobj.dump;
		else
			seqinfo->dobj.dump |= owning_tab->dobj.dump;

		/* Make sure that necessary data is available if we're dumping it */
		if (seqinfo->dobj.dump != DUMP_COMPONENT_NONE)
		{
			seqinfo->interesting = true;
			owning_tab->interesting = true;
		}
	}
}

/*
 * getInherits
 *	  read all the inheritance information
 * from the system catalogs return them in the InhInfo* structure
 *
 * numInherits is set to the number of pairs read in
 */
InhInfo *
getInherits(Archive *fout, int *numInherits)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query = createPQExpBuffer();
	InhInfo    *inhinfo;

	int			i_inhrelid;
	int			i_inhparent;

	/* find all the inheritance information */
	appendPQExpBufferStr(query, "SELECT inhrelid, inhparent FROM pg_inherits");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	*numInherits = ntups;

	inhinfo = (InhInfo *) pg_malloc(ntups * sizeof(InhInfo));

	i_inhrelid = PQfnumber(res, "inhrelid");
	i_inhparent = PQfnumber(res, "inhparent");

	for (i = 0; i < ntups; i++)
	{
		inhinfo[i].inhrelid = atooid(PQgetvalue(res, i, i_inhrelid));
		inhinfo[i].inhparent = atooid(PQgetvalue(res, i, i_inhparent));
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return inhinfo;
}

/*
 * getPartitioningInfo
 *	  get information about partitioning
 *
 * For the most part, we only collect partitioning info about tables we
 * intend to dump.  However, this function has to consider all partitioned
 * tables in the database, because we need to know about parents of partitions
 * we are going to dump even if the parents themselves won't be dumped.
 *
 * Specifically, what we need to know is whether each partitioned table
 * has an "unsafe" partitioning scheme that requires us to force
 * load-via-partition-root mode for its children.  Currently the only case
 * for which we force that is hash partitioning on enum columns, since the
 * hash codes depend on enum value OIDs which won't be replicated across
 * dump-and-reload.  There are other cases in which load-via-partition-root
 * might be necessary, but we expect users to cope with them.
 */
void
getPartitioningInfo(Archive *fout)
{
	PQExpBuffer query;
	PGresult   *res;
	int			ntups;

	/* hash partitioning didn't exist before v11 */
	if (fout->remoteVersion < 110000)
		return;
	/* needn't bother if schema-only dump */
	if (fout->dopt->schemaOnly)
		return;

	query = createPQExpBuffer();

	/*
	 * Unsafe partitioning schemes are exactly those for which hash enum_ops
	 * appears among the partition opclasses.  We needn't check partstrat.
	 *
	 * Note that this query may well retrieve info about tables we aren't
	 * going to dump and hence have no lock on.  That's okay since we need not
	 * invoke any unsafe server-side functions.
	 */
	appendPQExpBufferStr(query,
						 "SELECT partrelid FROM pg_partitioned_table WHERE\n"
						 "(SELECT c.oid FROM pg_opclass c JOIN pg_am a "
						 "ON c.opcmethod = a.oid\n"
						 "WHERE opcname = 'enum_ops' "
						 "AND opcnamespace = 'pg_catalog'::regnamespace "
						 "AND amname = 'hash') = ANY(partclass)");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	for (int i = 0; i < ntups; i++)
	{
		Oid			tabrelid = atooid(PQgetvalue(res, i, 0));
		TableInfo  *tbinfo;

		tbinfo = findTableByOid(tabrelid);
		if (tbinfo == NULL)
			pg_fatal("failed sanity check, table OID %u appearing in pg_partitioned_table not found",
					 tabrelid);
		tbinfo->unsafe_partitions = true;
	}

	PQclear(res);

	destroyPQExpBuffer(query);
}

/*
 * getIndexes
 *	  get information about every index on a dumpable table
 *
 * Note: index data is not returned directly to the caller, but it
 * does get entered into the DumpableObject tables.
 */
void
getIndexes(Archive *fout, TableInfo tblinfo[], int numTables)
{
	PQExpBuffer query = createPQExpBuffer();
	PQExpBuffer tbloids = createPQExpBuffer();
	PGresult   *res;
	int			ntups;
	int			curtblindx;
	IndxInfo   *indxinfo;
	int			i_tableoid,
				i_oid,
				i_indrelid,
				i_indexname,
				i_parentidx,
				i_indexdef,
				i_indnkeyatts,
				i_indnatts,
				i_indkey,
				i_indisclustered,
				i_indisreplident,
				i_indnullsnotdistinct,
				i_contype,
				i_conname,
				i_condeferrable,
				i_condeferred,
				i_contableoid,
				i_conoid,
				i_condef,
				i_tablespace,
				i_indreloptions,
				i_indstatcols,
				i_indstatvals;

	/*
	 * We want to perform just one query against pg_index.  However, we
	 * mustn't try to select every row of the catalog and then sort it out on
	 * the client side, because some of the server-side functions we need
	 * would be unsafe to apply to tables we don't have lock on.  Hence, we
	 * build an array of the OIDs of tables we care about (and now have lock
	 * on!), and use a WHERE clause to constrain which rows are selected.
	 */
	appendPQExpBufferChar(tbloids, '{');
	for (int i = 0; i < numTables; i++)
	{
		TableInfo  *tbinfo = &tblinfo[i];

		if (!tbinfo->hasindex)
			continue;

		/*
		 * We can ignore indexes of uninteresting tables.
		 */
		if (!tbinfo->interesting)
			continue;

		/* OK, we need info for this table */
		if (tbloids->len > 1)	/* do we have more than the '{'? */
			appendPQExpBufferChar(tbloids, ',');
		appendPQExpBuffer(tbloids, "%u", tbinfo->dobj.catId.oid);
	}
	appendPQExpBufferChar(tbloids, '}');

	appendPQExpBufferStr(query,
						 "SELECT t.tableoid, t.oid, i.indrelid, "
						 "t.relname AS indexname, "
						 "pg_catalog.pg_get_indexdef(i.indexrelid) AS indexdef, "
						 "i.indkey, i.indisclustered, "
						 "c.contype, c.conname, "
						 "c.condeferrable, c.condeferred, "
						 "c.tableoid AS contableoid, "
						 "c.oid AS conoid, "
						 "pg_catalog.pg_get_constraintdef(c.oid, false) AS condef, "
						 "(SELECT spcname FROM pg_catalog.pg_tablespace s WHERE s.oid = t.reltablespace) AS tablespace, "
						 "t.reloptions AS indreloptions, ");


	if (fout->remoteVersion >= 90400)
		appendPQExpBufferStr(query,
							 "i.indisreplident, ");
	else
		appendPQExpBufferStr(query,
							 "false AS indisreplident, ");

	if (fout->remoteVersion >= 110000)
		appendPQExpBufferStr(query,
							 "inh.inhparent AS parentidx, "
							 "i.indnkeyatts AS indnkeyatts, "
							 "i.indnatts AS indnatts, "
							 "(SELECT pg_catalog.array_agg(attnum ORDER BY attnum) "
							 "  FROM pg_catalog.pg_attribute "
							 "  WHERE attrelid = i.indexrelid AND "
							 "    attstattarget >= 0) AS indstatcols, "
							 "(SELECT pg_catalog.array_agg(attstattarget ORDER BY attnum) "
							 "  FROM pg_catalog.pg_attribute "
							 "  WHERE attrelid = i.indexrelid AND "
							 "    attstattarget >= 0) AS indstatvals, ");
	else
		appendPQExpBufferStr(query,
							 "0 AS parentidx, "
							 "i.indnatts AS indnkeyatts, "
							 "i.indnatts AS indnatts, "
							 "'' AS indstatcols, "
							 "'' AS indstatvals, ");

	if (fout->remoteVersion >= 150000)
		appendPQExpBufferStr(query,
							 "i.indnullsnotdistinct ");
	else
		appendPQExpBufferStr(query,
							 "false AS indnullsnotdistinct ");

	/*
	 * The point of the messy-looking outer join is to find a constraint that
	 * is related by an internal dependency link to the index. If we find one,
	 * create a CONSTRAINT entry linked to the INDEX entry.  We assume an
	 * index won't have more than one internal dependency.
	 *
	 * Note: the check on conrelid is redundant, but useful because that
	 * column is indexed while conindid is not.
	 */
	if (fout->remoteVersion >= 110000)
	{
		appendPQExpBuffer(query,
						  "FROM unnest('%s'::pg_catalog.oid[]) AS src(tbloid)\n"
						  "JOIN pg_catalog.pg_index i ON (src.tbloid = i.indrelid) "
						  "JOIN pg_catalog.pg_class t ON (t.oid = i.indexrelid) "
						  "JOIN pg_catalog.pg_class t2 ON (t2.oid = i.indrelid) "
						  "LEFT JOIN pg_catalog.pg_constraint c "
						  "ON (i.indrelid = c.conrelid AND "
						  "i.indexrelid = c.conindid AND "
						  "c.contype IN ('p','u','x')) "
						  "LEFT JOIN pg_catalog.pg_inherits inh "
						  "ON (inh.inhrelid = indexrelid) "
						  "WHERE (i.indisvalid OR t2.relkind = 'p') "
						  "AND i.indisready "
						  "ORDER BY i.indrelid, indexname",
						  tbloids->data);
	}
	else
	{
		/*
		 * the test on indisready is necessary in 9.2, and harmless in
		 * earlier/later versions
		 */
		appendPQExpBuffer(query,
						  "FROM unnest('%s'::pg_catalog.oid[]) AS src(tbloid)\n"
						  "JOIN pg_catalog.pg_index i ON (src.tbloid = i.indrelid) "
						  "JOIN pg_catalog.pg_class t ON (t.oid = i.indexrelid) "
						  "LEFT JOIN pg_catalog.pg_constraint c "
						  "ON (i.indrelid = c.conrelid AND "
						  "i.indexrelid = c.conindid AND "
						  "c.contype IN ('p','u','x')) "
						  "WHERE i.indisvalid AND i.indisready "
						  "ORDER BY i.indrelid, indexname",
						  tbloids->data);
	}

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_indrelid = PQfnumber(res, "indrelid");
	i_indexname = PQfnumber(res, "indexname");
	i_parentidx = PQfnumber(res, "parentidx");
	i_indexdef = PQfnumber(res, "indexdef");
	i_indnkeyatts = PQfnumber(res, "indnkeyatts");
	i_indnatts = PQfnumber(res, "indnatts");
	i_indkey = PQfnumber(res, "indkey");
	i_indisclustered = PQfnumber(res, "indisclustered");
	i_indisreplident = PQfnumber(res, "indisreplident");
	i_indnullsnotdistinct = PQfnumber(res, "indnullsnotdistinct");
	i_contype = PQfnumber(res, "contype");
	i_conname = PQfnumber(res, "conname");
	i_condeferrable = PQfnumber(res, "condeferrable");
	i_condeferred = PQfnumber(res, "condeferred");
	i_contableoid = PQfnumber(res, "contableoid");
	i_conoid = PQfnumber(res, "conoid");
	i_condef = PQfnumber(res, "condef");
	i_tablespace = PQfnumber(res, "tablespace");
	i_indreloptions = PQfnumber(res, "indreloptions");
	i_indstatcols = PQfnumber(res, "indstatcols");
	i_indstatvals = PQfnumber(res, "indstatvals");

	indxinfo = (IndxInfo *) pg_malloc(ntups * sizeof(IndxInfo));

	/*
	 * Outer loop iterates once per table, not once per row.  Incrementing of
	 * j is handled by the inner loop.
	 */
	curtblindx = -1;
	for (int j = 0; j < ntups;)
	{
		Oid			indrelid = atooid(PQgetvalue(res, j, i_indrelid));
		TableInfo  *tbinfo = NULL;
		int			numinds;

		/* Count rows for this table */
		for (numinds = 1; numinds < ntups - j; numinds++)
			if (atooid(PQgetvalue(res, j + numinds, i_indrelid)) != indrelid)
				break;

		/*
		 * Locate the associated TableInfo; we rely on tblinfo[] being in OID
		 * order.
		 */
		while (++curtblindx < numTables)
		{
			tbinfo = &tblinfo[curtblindx];
			if (tbinfo->dobj.catId.oid == indrelid)
				break;
		}
		if (curtblindx >= numTables)
			pg_fatal("unrecognized table OID %u", indrelid);
		/* cross-check that we only got requested tables */
		if (!tbinfo->hasindex ||
			!tbinfo->interesting)
			pg_fatal("unexpected index data for table \"%s\"",
					 tbinfo->dobj.name);

		/* Save data for this table */
		tbinfo->indexes = indxinfo + j;
		tbinfo->numIndexes = numinds;

		for (int c = 0; c < numinds; c++, j++)
		{
			char		contype;

			indxinfo[j].dobj.objType = DO_INDEX;
			indxinfo[j].dobj.catId.tableoid = atooid(PQgetvalue(res, j, i_tableoid));
			indxinfo[j].dobj.catId.oid = atooid(PQgetvalue(res, j, i_oid));
			AssignDumpId(&indxinfo[j].dobj);
			indxinfo[j].dobj.dump = tbinfo->dobj.dump;
			indxinfo[j].dobj.name = pg_strdup(PQgetvalue(res, j, i_indexname));
			indxinfo[j].dobj.namespace = tbinfo->dobj.namespace;
			indxinfo[j].indextable = tbinfo;
			indxinfo[j].indexdef = pg_strdup(PQgetvalue(res, j, i_indexdef));
			indxinfo[j].indnkeyattrs = atoi(PQgetvalue(res, j, i_indnkeyatts));
			indxinfo[j].indnattrs = atoi(PQgetvalue(res, j, i_indnatts));
			indxinfo[j].tablespace = pg_strdup(PQgetvalue(res, j, i_tablespace));
			indxinfo[j].indreloptions = pg_strdup(PQgetvalue(res, j, i_indreloptions));
			indxinfo[j].indstatcols = pg_strdup(PQgetvalue(res, j, i_indstatcols));
			indxinfo[j].indstatvals = pg_strdup(PQgetvalue(res, j, i_indstatvals));
			indxinfo[j].indkeys = (Oid *) pg_malloc(indxinfo[j].indnattrs * sizeof(Oid));
			parseOidArray(PQgetvalue(res, j, i_indkey),
						  indxinfo[j].indkeys, indxinfo[j].indnattrs);
			indxinfo[j].indisclustered = (PQgetvalue(res, j, i_indisclustered)[0] == 't');
			indxinfo[j].indisreplident = (PQgetvalue(res, j, i_indisreplident)[0] == 't');
			indxinfo[j].indnullsnotdistinct = (PQgetvalue(res, j, i_indnullsnotdistinct)[0] == 't');
			indxinfo[j].parentidx = atooid(PQgetvalue(res, j, i_parentidx));
			indxinfo[j].partattaches = (SimplePtrList)
			{
				NULL, NULL
			};
			contype = *(PQgetvalue(res, j, i_contype));

			if (contype == 'p' || contype == 'u' || contype == 'x')
			{
				/*
				 * If we found a constraint matching the index, create an
				 * entry for it.
				 */
				ConstraintInfo *constrinfo;

				constrinfo = (ConstraintInfo *) pg_malloc(sizeof(ConstraintInfo));
				constrinfo->dobj.objType = DO_CONSTRAINT;
				constrinfo->dobj.catId.tableoid = atooid(PQgetvalue(res, j, i_contableoid));
				constrinfo->dobj.catId.oid = atooid(PQgetvalue(res, j, i_conoid));
				AssignDumpId(&constrinfo->dobj);
				constrinfo->dobj.dump = tbinfo->dobj.dump;
				constrinfo->dobj.name = pg_strdup(PQgetvalue(res, j, i_conname));
				constrinfo->dobj.namespace = tbinfo->dobj.namespace;
				constrinfo->contable = tbinfo;
				constrinfo->condomain = NULL;
				constrinfo->contype = contype;
				if (contype == 'x')
					constrinfo->condef = pg_strdup(PQgetvalue(res, j, i_condef));
				else
					constrinfo->condef = NULL;
				constrinfo->confrelid = InvalidOid;
				constrinfo->conindex = indxinfo[j].dobj.dumpId;
				constrinfo->condeferrable = *(PQgetvalue(res, j, i_condeferrable)) == 't';
				constrinfo->condeferred = *(PQgetvalue(res, j, i_condeferred)) == 't';
				constrinfo->conislocal = true;
				constrinfo->separate = true;

				indxinfo[j].indexconstraint = constrinfo->dobj.dumpId;
			}
			else
			{
				/* Plain secondary index */
				indxinfo[j].indexconstraint = 0;
			}
		}
	}

	PQclear(res);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(tbloids);
}

/*
 * getExtendedStatistics
 *	  get information about extended-statistics objects.
 *
 * Note: extended statistics data is not returned directly to the caller, but
 * it does get entered into the DumpableObject tables.
 */
void
getExtendedStatistics(Archive *fout)
{
	PQExpBuffer query;
	PGresult   *res;
	StatsExtInfo *statsextinfo;
	int			ntups;
	int			i_tableoid;
	int			i_oid;
	int			i_stxname;
	int			i_stxnamespace;
	int			i_stxowner;
	int			i_stxrelid;
	int			i_stattarget;
	int			i;

	/* Extended statistics were new in v10 */
	if (fout->remoteVersion < 100000)
		return;

	query = createPQExpBuffer();

	if (fout->remoteVersion < 130000)
		appendPQExpBufferStr(query, "SELECT tableoid, oid, stxname, "
							 "stxnamespace, stxowner, stxrelid, NULL AS stxstattarget "
							 "FROM pg_catalog.pg_statistic_ext");
	else
		appendPQExpBufferStr(query, "SELECT tableoid, oid, stxname, "
							 "stxnamespace, stxowner, stxrelid, stxstattarget "
							 "FROM pg_catalog.pg_statistic_ext");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_stxname = PQfnumber(res, "stxname");
	i_stxnamespace = PQfnumber(res, "stxnamespace");
	i_stxowner = PQfnumber(res, "stxowner");
	i_stxrelid = PQfnumber(res, "stxrelid");
	i_stattarget = PQfnumber(res, "stxstattarget");

	statsextinfo = (StatsExtInfo *) pg_malloc(ntups * sizeof(StatsExtInfo));

	for (i = 0; i < ntups; i++)
	{
		statsextinfo[i].dobj.objType = DO_STATSEXT;
		statsextinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		statsextinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&statsextinfo[i].dobj);
		statsextinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_stxname));
		statsextinfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_stxnamespace)));
		statsextinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_stxowner));
		statsextinfo[i].stattable =
			findTableByOid(atooid(PQgetvalue(res, i, i_stxrelid)));
		if (PQgetisnull(res, i, i_stattarget))
			statsextinfo[i].stattarget = -1;
		else
			statsextinfo[i].stattarget = atoi(PQgetvalue(res, i, i_stattarget));

		/* Decide whether we want to dump it */
		selectDumpableStatisticsObject(&(statsextinfo[i]), fout);
	}

	PQclear(res);
	destroyPQExpBuffer(query);
}

/*
 * getConstraints
 *
 * Get info about constraints on dumpable tables.
 *
 * Currently handles foreign keys only.
 * Unique and primary key constraints are handled with indexes,
 * while check constraints are processed in getTableAttrs().
 */
void
getConstraints(Archive *fout, TableInfo tblinfo[], int numTables)
{
	PQExpBuffer query = createPQExpBuffer();
	PQExpBuffer tbloids = createPQExpBuffer();
	PGresult   *res;
	int			ntups;
	int			curtblindx;
	TableInfo  *tbinfo = NULL;
	ConstraintInfo *constrinfo;
	int			i_contableoid,
				i_conoid,
				i_conrelid,
				i_conname,
				i_confrelid,
				i_conindid,
				i_condef;

	/*
	 * We want to perform just one query against pg_constraint.  However, we
	 * mustn't try to select every row of the catalog and then sort it out on
	 * the client side, because some of the server-side functions we need
	 * would be unsafe to apply to tables we don't have lock on.  Hence, we
	 * build an array of the OIDs of tables we care about (and now have lock
	 * on!), and use a WHERE clause to constrain which rows are selected.
	 */
	appendPQExpBufferChar(tbloids, '{');
	for (int i = 0; i < numTables; i++)
	{
		TableInfo  *tinfo = &tblinfo[i];

		/*
		 * For partitioned tables, foreign keys have no triggers so they must
		 * be included anyway in case some foreign keys are defined.
		 */
		if ((!tinfo->hastriggers &&
			 tinfo->relkind != RELKIND_PARTITIONED_TABLE) ||
			!(tinfo->dobj.dump & DUMP_COMPONENT_DEFINITION))
			continue;

		/* OK, we need info for this table */
		if (tbloids->len > 1)	/* do we have more than the '{'? */
			appendPQExpBufferChar(tbloids, ',');
		appendPQExpBuffer(tbloids, "%u", tinfo->dobj.catId.oid);
	}
	appendPQExpBufferChar(tbloids, '}');

	appendPQExpBufferStr(query,
						 "SELECT c.tableoid, c.oid, "
						 "conrelid, conname, confrelid, ");
	if (fout->remoteVersion >= 110000)
		appendPQExpBufferStr(query, "conindid, ");
	else
		appendPQExpBufferStr(query, "0 AS conindid, ");
	appendPQExpBuffer(query,
					  "pg_catalog.pg_get_constraintdef(c.oid) AS condef\n"
					  "FROM unnest('%s'::pg_catalog.oid[]) AS src(tbloid)\n"
					  "JOIN pg_catalog.pg_constraint c ON (src.tbloid = c.conrelid)\n"
					  "WHERE contype = 'f' ",
					  tbloids->data);
	if (fout->remoteVersion >= 110000)
		appendPQExpBufferStr(query,
							 "AND conparentid = 0 ");
	appendPQExpBufferStr(query,
						 "ORDER BY conrelid, conname");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_contableoid = PQfnumber(res, "tableoid");
	i_conoid = PQfnumber(res, "oid");
	i_conrelid = PQfnumber(res, "conrelid");
	i_conname = PQfnumber(res, "conname");
	i_confrelid = PQfnumber(res, "confrelid");
	i_conindid = PQfnumber(res, "conindid");
	i_condef = PQfnumber(res, "condef");

	constrinfo = (ConstraintInfo *) pg_malloc(ntups * sizeof(ConstraintInfo));

	curtblindx = -1;
	for (int j = 0; j < ntups; j++)
	{
		Oid			conrelid = atooid(PQgetvalue(res, j, i_conrelid));
		TableInfo  *reftable;

		/*
		 * Locate the associated TableInfo; we rely on tblinfo[] being in OID
		 * order.
		 */
		if (tbinfo == NULL || tbinfo->dobj.catId.oid != conrelid)
		{
			while (++curtblindx < numTables)
			{
				tbinfo = &tblinfo[curtblindx];
				if (tbinfo->dobj.catId.oid == conrelid)
					break;
			}
			if (curtblindx >= numTables)
				pg_fatal("unrecognized table OID %u", conrelid);
		}

		constrinfo[j].dobj.objType = DO_FK_CONSTRAINT;
		constrinfo[j].dobj.catId.tableoid = atooid(PQgetvalue(res, j, i_contableoid));
		constrinfo[j].dobj.catId.oid = atooid(PQgetvalue(res, j, i_conoid));
		AssignDumpId(&constrinfo[j].dobj);
		constrinfo[j].dobj.name = pg_strdup(PQgetvalue(res, j, i_conname));
		constrinfo[j].dobj.namespace = tbinfo->dobj.namespace;
		constrinfo[j].contable = tbinfo;
		constrinfo[j].condomain = NULL;
		constrinfo[j].contype = 'f';
		constrinfo[j].condef = pg_strdup(PQgetvalue(res, j, i_condef));
		constrinfo[j].confrelid = atooid(PQgetvalue(res, j, i_confrelid));
		constrinfo[j].conindex = 0;
		constrinfo[j].condeferrable = false;
		constrinfo[j].condeferred = false;
		constrinfo[j].conislocal = true;
		constrinfo[j].separate = true;

		/*
		 * Restoring an FK that points to a partitioned table requires that
		 * all partition indexes have been attached beforehand. Ensure that
		 * happens by making the constraint depend on each index partition
		 * attach object.
		 */
		reftable = findTableByOid(constrinfo[j].confrelid);
		if (reftable && reftable->relkind == RELKIND_PARTITIONED_TABLE)
		{
			Oid			indexOid = atooid(PQgetvalue(res, j, i_conindid));

			if (indexOid != InvalidOid)
			{
				for (int k = 0; k < reftable->numIndexes; k++)
				{
					IndxInfo   *refidx;

					/* not our index? */
					if (reftable->indexes[k].dobj.catId.oid != indexOid)
						continue;

					refidx = &reftable->indexes[k];
					addConstrChildIdxDeps(&constrinfo[j].dobj, refidx);
					break;
				}
			}
		}
	}

	PQclear(res);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(tbloids);
}

/*
 * addConstrChildIdxDeps
 *
 * Recursive subroutine for getConstraints
 *
 * Given an object representing a foreign key constraint and an index on the
 * partitioned table it references, mark the constraint object as dependent
 * on the DO_INDEX_ATTACH object of each index partition, recursively
 * drilling down to their partitions if any.  This ensures that the FK is not
 * restored until the index is fully marked valid.
 */
static void
addConstrChildIdxDeps(DumpableObject *dobj, const IndxInfo *refidx)
{
	SimplePtrListCell *cell;

	Assert(dobj->objType == DO_FK_CONSTRAINT);

	for (cell = refidx->partattaches.head; cell; cell = cell->next)
	{
		IndexAttachInfo *attach = (IndexAttachInfo *) cell->ptr;

		addObjectDependency(dobj, attach->dobj.dumpId);

		if (attach->partitionIdx->partattaches.head != NULL)
			addConstrChildIdxDeps(dobj, attach->partitionIdx);
	}
}

/*
 * getDomainConstraints
 *
 * Get info about constraints on a domain.
 */
static void
getDomainConstraints(Archive *fout, TypeInfo *tyinfo)
{
	int			i;
	ConstraintInfo *constrinfo;
	PQExpBuffer query = createPQExpBuffer();
	PGresult   *res;
	int			i_tableoid,
				i_oid,
				i_conname,
				i_consrc;
	int			ntups;

	if (!fout->is_prepared[PREPQUERY_GETDOMAINCONSTRAINTS])
	{
		/* Set up query for constraint-specific details */
		appendPQExpBufferStr(query,
							 "PREPARE getDomainConstraints(pg_catalog.oid) AS\n"
							 "SELECT tableoid, oid, conname, "
							 "pg_catalog.pg_get_constraintdef(oid) AS consrc, "
							 "convalidated "
							 "FROM pg_catalog.pg_constraint "
							 "WHERE contypid = $1 AND contype = 'c' "
							 "ORDER BY conname");

		ExecuteSqlStatement(fout, query->data);

		fout->is_prepared[PREPQUERY_GETDOMAINCONSTRAINTS] = true;
	}

	printfPQExpBuffer(query,
					  "EXECUTE getDomainConstraints('%u')",
					  tyinfo->dobj.catId.oid);

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_conname = PQfnumber(res, "conname");
	i_consrc = PQfnumber(res, "consrc");

	constrinfo = (ConstraintInfo *) pg_malloc(ntups * sizeof(ConstraintInfo));

	tyinfo->nDomChecks = ntups;
	tyinfo->domChecks = constrinfo;

	for (i = 0; i < ntups; i++)
	{
		bool		validated = PQgetvalue(res, i, 4)[0] == 't';

		constrinfo[i].dobj.objType = DO_CONSTRAINT;
		constrinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		constrinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&constrinfo[i].dobj);
		constrinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_conname));
		constrinfo[i].dobj.namespace = tyinfo->dobj.namespace;
		constrinfo[i].contable = NULL;
		constrinfo[i].condomain = tyinfo;
		constrinfo[i].contype = 'c';
		constrinfo[i].condef = pg_strdup(PQgetvalue(res, i, i_consrc));
		constrinfo[i].confrelid = InvalidOid;
		constrinfo[i].conindex = 0;
		constrinfo[i].condeferrable = false;
		constrinfo[i].condeferred = false;
		constrinfo[i].conislocal = true;

		constrinfo[i].separate = !validated;

		/*
		 * Make the domain depend on the constraint, ensuring it won't be
		 * output till any constraint dependencies are OK.  If the constraint
		 * has not been validated, it's going to be dumped after the domain
		 * anyway, so this doesn't matter.
		 */
		if (validated)
			addObjectDependency(&tyinfo->dobj,
								constrinfo[i].dobj.dumpId);
	}

	PQclear(res);

	destroyPQExpBuffer(query);
}

/*
 * getRules
 *	  get basic information about every rule in the system
 *
 * numRules is set to the number of rules read in
 */
RuleInfo *
getRules(Archive *fout, int *numRules)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query = createPQExpBuffer();
	RuleInfo   *ruleinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_rulename;
	int			i_ruletable;
	int			i_ev_type;
	int			i_is_instead;
	int			i_ev_enabled;

	appendPQExpBufferStr(query, "SELECT "
						 "tableoid, oid, rulename, "
						 "ev_class AS ruletable, ev_type, is_instead, "
						 "ev_enabled "
						 "FROM pg_rewrite "
						 "ORDER BY oid");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	*numRules = ntups;

	ruleinfo = (RuleInfo *) pg_malloc(ntups * sizeof(RuleInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_rulename = PQfnumber(res, "rulename");
	i_ruletable = PQfnumber(res, "ruletable");
	i_ev_type = PQfnumber(res, "ev_type");
	i_is_instead = PQfnumber(res, "is_instead");
	i_ev_enabled = PQfnumber(res, "ev_enabled");

	for (i = 0; i < ntups; i++)
	{
		Oid			ruletableoid;

		ruleinfo[i].dobj.objType = DO_RULE;
		ruleinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		ruleinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&ruleinfo[i].dobj);
		ruleinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_rulename));
		ruletableoid = atooid(PQgetvalue(res, i, i_ruletable));
		ruleinfo[i].ruletable = findTableByOid(ruletableoid);
		if (ruleinfo[i].ruletable == NULL)
			pg_fatal("failed sanity check, parent table with OID %u of pg_rewrite entry with OID %u not found",
					 ruletableoid, ruleinfo[i].dobj.catId.oid);
		ruleinfo[i].dobj.namespace = ruleinfo[i].ruletable->dobj.namespace;
		ruleinfo[i].dobj.dump = ruleinfo[i].ruletable->dobj.dump;
		ruleinfo[i].ev_type = *(PQgetvalue(res, i, i_ev_type));
		ruleinfo[i].is_instead = *(PQgetvalue(res, i, i_is_instead)) == 't';
		ruleinfo[i].ev_enabled = *(PQgetvalue(res, i, i_ev_enabled));
		if (ruleinfo[i].ruletable)
		{
			/*
			 * If the table is a view or materialized view, force its ON
			 * SELECT rule to be sorted before the view itself --- this
			 * ensures that any dependencies for the rule affect the table's
			 * positioning. Other rules are forced to appear after their
			 * table.
			 */
			if ((ruleinfo[i].ruletable->relkind == RELKIND_VIEW ||
				 ruleinfo[i].ruletable->relkind == RELKIND_MATVIEW) &&
				ruleinfo[i].ev_type == '1' && ruleinfo[i].is_instead)
			{
				addObjectDependency(&ruleinfo[i].ruletable->dobj,
									ruleinfo[i].dobj.dumpId);
				/* We'll merge the rule into CREATE VIEW, if possible */
				ruleinfo[i].separate = false;
			}
			else
			{
				addObjectDependency(&ruleinfo[i].dobj,
									ruleinfo[i].ruletable->dobj.dumpId);
				ruleinfo[i].separate = true;
			}
		}
		else
			ruleinfo[i].separate = true;
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return ruleinfo;
}

/*
 * getTriggers
 *	  get information about every trigger on a dumpable table
 *
 * Note: trigger data is not returned directly to the caller, but it
 * does get entered into the DumpableObject tables.
 */

void mark_views_for_dump(TableInfo *tblinfo, int numTables, QueryDependencies *deps)
{
    if (!tblinfo || !deps || deps->viewCount == 0) return;
	
    for (int i = 0; i < numTables; i++) {
        TableInfo *tbinfo = &tblinfo[i];
		
        if (tbinfo->relkind == 'v' || tbinfo->relkind == 'm') {
			
            if (is_in_view_oids(tbinfo->dobj.catId.oid)) {
				
               
                tbinfo->dobj.dump |= DUMP_COMPONENT_DEFINITION;
                
                

                
            }
        }
    }
}


void mark_schemas_for_dump(TableInfo *tblinfo, int numTables)
{
    if (!tblinfo)
        return;

    for (int i = 0; i < numTables; i++)
    {
        if (tblinfo[i].dobj.dump & DUMP_COMPONENT_DEFINITION)
        {
            NamespaceInfo *ns = tblinfo[i].dobj.namespace;

            if (ns)
            {
                if (strcmp(ns->dobj.name, "public") == 0 ||
                    strncmp(ns->dobj.name, "pg_", 3) == 0 ||
                    strcmp(ns->dobj.name, "information_schema") == 0)
                {
                    continue;
                }

                if (!(ns->dobj.dump & DUMP_COMPONENT_DEFINITION))
                {
                    ns->dobj.dump |= DUMP_COMPONENT_DEFINITION;
                    
                    
                    
                    pg_log_info("Schema '%s' exported for table '%s'", 
                                 ns->dobj.name, tblinfo[i].dobj.name);
                }
            }
        }
    }
}

void
getTriggers(Archive *fout, TableInfo tblinfo[], int numTables)
{
	PQExpBuffer query = createPQExpBuffer();
	PQExpBuffer tbloids = createPQExpBuffer();
	PGresult   *res;
	int			ntups;
	int			curtblindx;
	TriggerInfo *tginfo;
	int			i_tableoid,
				i_oid,
				i_tgrelid,
				i_tgname,
				i_tgenabled,
				i_tgispartition,
				i_tgdef;

	/*
	 * We want to perform just one query against pg_trigger.  However, we
	 * mustn't try to select every row of the catalog and then sort it out on
	 * the client side, because some of the server-side functions we need
	 * would be unsafe to apply to tables we don't have lock on.  Hence, we
	 * build an array of the OIDs of tables we care about (and now have lock
	 * on!), and use a WHERE clause to constrain which rows are selected.
	 */
	appendPQExpBufferChar(tbloids, '{');
	for (int i = 0; i < numTables; i++)
	{
		TableInfo  *tbinfo = &tblinfo[i];

		if (!tbinfo->hastriggers ||
			!(tbinfo->dobj.dump & DUMP_COMPONENT_DEFINITION))
			continue;

		/* OK, we need info for this table */
		if (tbloids->len > 1)	/* do we have more than the '{'? */
			appendPQExpBufferChar(tbloids, ',');
		appendPQExpBuffer(tbloids, "%u", tbinfo->dobj.catId.oid);
	}
	appendPQExpBufferChar(tbloids, '}');

	if (fout->remoteVersion >= 150000)
	{
		/*
		 * NB: think not to use pretty=true in pg_get_triggerdef.  It could
		 * result in non-forward-compatible dumps of WHEN clauses due to
		 * under-parenthesization.
		 *
		 * NB: We need to see partition triggers in case the tgenabled flag
		 * has been changed from the parent.
		 */
		appendPQExpBuffer(query,
						  "SELECT t.tgrelid, t.tgname, "
						  "pg_catalog.pg_get_triggerdef(t.oid, false) AS tgdef, "
						  "t.tgenabled, t.tableoid, t.oid, "
						  "t.tgparentid <> 0 AS tgispartition\n"
						  "FROM unnest('%s'::pg_catalog.oid[]) AS src(tbloid)\n"
						  "JOIN pg_catalog.pg_trigger t ON (src.tbloid = t.tgrelid) "
						  "LEFT JOIN pg_catalog.pg_trigger u ON (u.oid = t.tgparentid) "
						  "WHERE ((NOT t.tgisinternal AND t.tgparentid = 0) "
						  "OR t.tgenabled != u.tgenabled) "
						  "ORDER BY t.tgrelid, t.tgname",
						  tbloids->data);
	}
	else if (fout->remoteVersion >= 130000)
	{
		/*
		 * NB: think not to use pretty=true in pg_get_triggerdef.  It could
		 * result in non-forward-compatible dumps of WHEN clauses due to
		 * under-parenthesization.
		 *
		 * NB: We need to see tgisinternal triggers in partitions, in case the
		 * tgenabled flag has been changed from the parent.
		 */
		appendPQExpBuffer(query,
						  "SELECT t.tgrelid, t.tgname, "
						  "pg_catalog.pg_get_triggerdef(t.oid, false) AS tgdef, "
						  "t.tgenabled, t.tableoid, t.oid, t.tgisinternal as tgispartition\n"
						  "FROM unnest('%s'::pg_catalog.oid[]) AS src(tbloid)\n"
						  "JOIN pg_catalog.pg_trigger t ON (src.tbloid = t.tgrelid) "
						  "LEFT JOIN pg_catalog.pg_trigger u ON (u.oid = t.tgparentid) "
						  "WHERE (NOT t.tgisinternal OR t.tgenabled != u.tgenabled) "
						  "ORDER BY t.tgrelid, t.tgname",
						  tbloids->data);
	}
	else if (fout->remoteVersion >= 110000)
	{
		/*
		 * NB: We need to see tgisinternal triggers in partitions, in case the
		 * tgenabled flag has been changed from the parent. No tgparentid in
		 * version 11-12, so we have to match them via pg_depend.
		 *
		 * See above about pretty=true in pg_get_triggerdef.
		 */
		appendPQExpBuffer(query,
						  "SELECT t.tgrelid, t.tgname, "
						  "pg_catalog.pg_get_triggerdef(t.oid, false) AS tgdef, "
						  "t.tgenabled, t.tableoid, t.oid, t.tgisinternal as tgispartition "
						  "FROM unnest('%s'::pg_catalog.oid[]) AS src(tbloid)\n"
						  "JOIN pg_catalog.pg_trigger t ON (src.tbloid = t.tgrelid) "
						  "LEFT JOIN pg_catalog.pg_depend AS d ON "
						  " d.classid = 'pg_catalog.pg_trigger'::pg_catalog.regclass AND "
						  " d.refclassid = 'pg_catalog.pg_trigger'::pg_catalog.regclass AND "
						  " d.objid = t.oid "
						  "LEFT JOIN pg_catalog.pg_trigger AS pt ON pt.oid = refobjid "
						  "WHERE (NOT t.tgisinternal OR t.tgenabled != pt.tgenabled) "
						  "ORDER BY t.tgrelid, t.tgname",
						  tbloids->data);
	}
	else
	{
		/* See above about pretty=true in pg_get_triggerdef */
		appendPQExpBuffer(query,
						  "SELECT t.tgrelid, t.tgname, "
						  "pg_catalog.pg_get_triggerdef(t.oid, false) AS tgdef, "
						  "t.tgenabled, false as tgispartition, "
						  "t.tableoid, t.oid "
						  "FROM unnest('%s'::pg_catalog.oid[]) AS src(tbloid)\n"
						  "JOIN pg_catalog.pg_trigger t ON (src.tbloid = t.tgrelid) "
						  "WHERE NOT tgisinternal "
						  "ORDER BY t.tgrelid, t.tgname",
						  tbloids->data);
	}

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_tgrelid = PQfnumber(res, "tgrelid");
	i_tgname = PQfnumber(res, "tgname");
	i_tgenabled = PQfnumber(res, "tgenabled");
	i_tgispartition = PQfnumber(res, "tgispartition");
	i_tgdef = PQfnumber(res, "tgdef");

	tginfo = (TriggerInfo *) pg_malloc(ntups * sizeof(TriggerInfo));

	/*
	 * Outer loop iterates once per table, not once per row.  Incrementing of
	 * j is handled by the inner loop.
	 */
	curtblindx = -1;
	for (int j = 0; j < ntups;)
	{
		Oid			tgrelid = atooid(PQgetvalue(res, j, i_tgrelid));
		TableInfo  *tbinfo = NULL;
		int			numtrigs;

		/* Count rows for this table */
		for (numtrigs = 1; numtrigs < ntups - j; numtrigs++)
			if (atooid(PQgetvalue(res, j + numtrigs, i_tgrelid)) != tgrelid)
				break;

		/*
		 * Locate the associated TableInfo; we rely on tblinfo[] being in OID
		 * order.
		 */
		while (++curtblindx < numTables)
		{
			tbinfo = &tblinfo[curtblindx];
			if (tbinfo->dobj.catId.oid == tgrelid)
				break;
		}
		if (curtblindx >= numTables)
			pg_fatal("unrecognized table OID %u", tgrelid);

		/* Save data for this table */
		tbinfo->triggers = tginfo + j;
		tbinfo->numTriggers = numtrigs;

		for (int c = 0; c < numtrigs; c++, j++)
		{
			tginfo[j].dobj.objType = DO_TRIGGER;
			tginfo[j].dobj.catId.tableoid = atooid(PQgetvalue(res, j, i_tableoid));
			tginfo[j].dobj.catId.oid = atooid(PQgetvalue(res, j, i_oid));
			AssignDumpId(&tginfo[j].dobj);
			tginfo[j].dobj.name = pg_strdup(PQgetvalue(res, j, i_tgname));
			tginfo[j].dobj.namespace = tbinfo->dobj.namespace;
			tginfo[j].tgtable = tbinfo;
			tginfo[j].tgenabled = *(PQgetvalue(res, j, i_tgenabled));
			tginfo[j].tgispartition = *(PQgetvalue(res, j, i_tgispartition)) == 't';
			tginfo[j].tgdef = pg_strdup(PQgetvalue(res, j, i_tgdef));
		}
	}

	PQclear(res);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(tbloids);
}

/*
 * getEventTriggers
 *	  get information about event triggers
 */
EventTriggerInfo *
getEventTriggers(Archive *fout, int *numEventTriggers)
{
	int			i;
	PQExpBuffer query;
	PGresult   *res;
	EventTriggerInfo *evtinfo;
	int			i_tableoid,
				i_oid,
				i_evtname,
				i_evtevent,
				i_evtowner,
				i_evttags,
				i_evtfname,
				i_evtenabled;
	int			ntups;

	/* Before 9.3, there are no event triggers */
	if (fout->remoteVersion < 90300)
	{
		*numEventTriggers = 0;
		return NULL;
	}

	query = createPQExpBuffer();

	appendPQExpBufferStr(query,
						 "SELECT e.tableoid, e.oid, evtname, evtenabled, "
						 "evtevent, evtowner, "
						 "array_to_string(array("
						 "select quote_literal(x) "
						 " from unnest(evttags) as t(x)), ', ') as evttags, "
						 "e.evtfoid::regproc as evtfname "
						 "FROM pg_event_trigger e "
						 "ORDER BY e.oid");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	*numEventTriggers = ntups;

	evtinfo = (EventTriggerInfo *) pg_malloc(ntups * sizeof(EventTriggerInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_evtname = PQfnumber(res, "evtname");
	i_evtevent = PQfnumber(res, "evtevent");
	i_evtowner = PQfnumber(res, "evtowner");
	i_evttags = PQfnumber(res, "evttags");
	i_evtfname = PQfnumber(res, "evtfname");
	i_evtenabled = PQfnumber(res, "evtenabled");

	for (i = 0; i < ntups; i++)
	{
		evtinfo[i].dobj.objType = DO_EVENT_TRIGGER;
		evtinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		evtinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&evtinfo[i].dobj);
		evtinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_evtname));
		evtinfo[i].evtname = pg_strdup(PQgetvalue(res, i, i_evtname));
		evtinfo[i].evtevent = pg_strdup(PQgetvalue(res, i, i_evtevent));
		evtinfo[i].evtowner = getRoleName(PQgetvalue(res, i, i_evtowner));
		evtinfo[i].evttags = pg_strdup(PQgetvalue(res, i, i_evttags));
		evtinfo[i].evtfname = pg_strdup(PQgetvalue(res, i, i_evtfname));
		evtinfo[i].evtenabled = *(PQgetvalue(res, i, i_evtenabled));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(evtinfo[i].dobj), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return evtinfo;
}

/*
 * getProcLangs
 *	  get basic information about every procedural language in the system
 *
 * numProcLangs is set to the number of langs read in
 *
 * NB: this must run after getFuncs() because we assume we can do
 * findFuncByOid().
 */
ProcLangInfo *
getProcLangs(Archive *fout, int *numProcLangs)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query = createPQExpBuffer();
	ProcLangInfo *planginfo;
	int			i_tableoid;
	int			i_oid;
	int			i_lanname;
	int			i_lanpltrusted;
	int			i_lanplcallfoid;
	int			i_laninline;
	int			i_lanvalidator;
	int			i_lanacl;
	int			i_acldefault;
	int			i_lanowner;

	appendPQExpBufferStr(query, "SELECT tableoid, oid, "
						 "lanname, lanpltrusted, lanplcallfoid, "
						 "laninline, lanvalidator, "
						 "lanacl, "
						 "acldefault('l', lanowner) AS acldefault, "
						 "lanowner "
						 "FROM pg_language "
						 "WHERE lanispl "
						 "ORDER BY oid");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	*numProcLangs = ntups;

	planginfo = (ProcLangInfo *) pg_malloc(ntups * sizeof(ProcLangInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_lanname = PQfnumber(res, "lanname");
	i_lanpltrusted = PQfnumber(res, "lanpltrusted");
	i_lanplcallfoid = PQfnumber(res, "lanplcallfoid");
	i_laninline = PQfnumber(res, "laninline");
	i_lanvalidator = PQfnumber(res, "lanvalidator");
	i_lanacl = PQfnumber(res, "lanacl");
	i_acldefault = PQfnumber(res, "acldefault");
	i_lanowner = PQfnumber(res, "lanowner");

	for (i = 0; i < ntups; i++)
	{
		planginfo[i].dobj.objType = DO_PROCLANG;
		planginfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		planginfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&planginfo[i].dobj);

		planginfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_lanname));
		planginfo[i].dacl.acl = pg_strdup(PQgetvalue(res, i, i_lanacl));
		planginfo[i].dacl.acldefault = pg_strdup(PQgetvalue(res, i, i_acldefault));
		planginfo[i].dacl.privtype = 0;
		planginfo[i].dacl.initprivs = NULL;
		planginfo[i].lanpltrusted = *(PQgetvalue(res, i, i_lanpltrusted)) == 't';
		planginfo[i].lanplcallfoid = atooid(PQgetvalue(res, i, i_lanplcallfoid));
		planginfo[i].laninline = atooid(PQgetvalue(res, i, i_laninline));
		planginfo[i].lanvalidator = atooid(PQgetvalue(res, i, i_lanvalidator));
		planginfo[i].lanowner = getRoleName(PQgetvalue(res, i, i_lanowner));

		/* Decide whether we want to dump it */
		selectDumpableProcLang(&(planginfo[i]), fout);

		/* Mark whether language has an ACL */
		if (!PQgetisnull(res, i, i_lanacl))
			planginfo[i].dobj.components |= DUMP_COMPONENT_ACL;
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return planginfo;
}

/*
 * getCasts
 *	  get basic information about most casts in the system
 *
 * numCasts is set to the number of casts read in
 *
 * Skip casts from a range to its multirange, since we'll create those
 * automatically.
 */
CastInfo *
getCasts(Archive *fout, int *numCasts)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query = createPQExpBuffer();
	CastInfo   *castinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_castsource;
	int			i_casttarget;
	int			i_castfunc;
	int			i_castcontext;
	int			i_castmethod;

	if (fout->remoteVersion >= 140000)
	{
		appendPQExpBufferStr(query, "SELECT tableoid, oid, "
							 "castsource, casttarget, castfunc, castcontext, "
							 "castmethod "
							 "FROM pg_cast c "
							 "WHERE NOT EXISTS ( "
							 "SELECT 1 FROM pg_range r "
							 "WHERE c.castsource = r.rngtypid "
							 "AND c.casttarget = r.rngmultitypid "
							 ") "
							 "ORDER BY 3,4");
	}
	else
	{
		appendPQExpBufferStr(query, "SELECT tableoid, oid, "
							 "castsource, casttarget, castfunc, castcontext, "
							 "castmethod "
							 "FROM pg_cast ORDER BY 3,4");
	}

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	*numCasts = ntups;

	castinfo = (CastInfo *) pg_malloc(ntups * sizeof(CastInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_castsource = PQfnumber(res, "castsource");
	i_casttarget = PQfnumber(res, "casttarget");
	i_castfunc = PQfnumber(res, "castfunc");
	i_castcontext = PQfnumber(res, "castcontext");
	i_castmethod = PQfnumber(res, "castmethod");

	for (i = 0; i < ntups; i++)
	{
		PQExpBufferData namebuf;
		TypeInfo   *sTypeInfo;
		TypeInfo   *tTypeInfo;

		castinfo[i].dobj.objType = DO_CAST;
		castinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		castinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&castinfo[i].dobj);
		castinfo[i].castsource = atooid(PQgetvalue(res, i, i_castsource));
		castinfo[i].casttarget = atooid(PQgetvalue(res, i, i_casttarget));
		castinfo[i].castfunc = atooid(PQgetvalue(res, i, i_castfunc));
		castinfo[i].castcontext = *(PQgetvalue(res, i, i_castcontext));
		castinfo[i].castmethod = *(PQgetvalue(res, i, i_castmethod));

		/*
		 * Try to name cast as concatenation of typnames.  This is only used
		 * for purposes of sorting.  If we fail to find either type, the name
		 * will be an empty string.
		 */
		initPQExpBuffer(&namebuf);
		sTypeInfo = findTypeByOid(castinfo[i].castsource);
		tTypeInfo = findTypeByOid(castinfo[i].casttarget);
		if (sTypeInfo && tTypeInfo)
			appendPQExpBuffer(&namebuf, "%s %s",
							  sTypeInfo->dobj.name, tTypeInfo->dobj.name);
		castinfo[i].dobj.name = namebuf.data;

		/* Decide whether we want to dump it */
		selectDumpableCast(&(castinfo[i]), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return castinfo;
}

static char *
get_language_name(Archive *fout, Oid langid)
{
	PQExpBuffer query;
	PGresult   *res;
	char	   *lanname;

	query = createPQExpBuffer();
	appendPQExpBuffer(query, "SELECT lanname FROM pg_language WHERE oid = %u", langid);
	res = ExecuteSqlQueryForSingleRow(fout, query->data);
	lanname = pg_strdup(fmtId(PQgetvalue(res, 0, 0)));
	destroyPQExpBuffer(query);
	PQclear(res);

	return lanname;
}

/*
 * getTransforms
 *	  get basic information about every transform in the system
 *
 * numTransforms is set to the number of transforms read in
 */
TransformInfo *
getTransforms(Archive *fout, int *numTransforms)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	TransformInfo *transforminfo;
	int			i_tableoid;
	int			i_oid;
	int			i_trftype;
	int			i_trflang;
	int			i_trffromsql;
	int			i_trftosql;

	/* Transforms didn't exist pre-9.5 */
	if (fout->remoteVersion < 90500)
	{
		*numTransforms = 0;
		return NULL;
	}

	query = createPQExpBuffer();

	appendPQExpBufferStr(query, "SELECT tableoid, oid, "
						 "trftype, trflang, trffromsql::oid, trftosql::oid "
						 "FROM pg_transform "
						 "ORDER BY 3,4");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	*numTransforms = ntups;

	transforminfo = (TransformInfo *) pg_malloc(ntups * sizeof(TransformInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_trftype = PQfnumber(res, "trftype");
	i_trflang = PQfnumber(res, "trflang");
	i_trffromsql = PQfnumber(res, "trffromsql");
	i_trftosql = PQfnumber(res, "trftosql");

	for (i = 0; i < ntups; i++)
	{
		PQExpBufferData namebuf;
		TypeInfo   *typeInfo;
		char	   *lanname;

		transforminfo[i].dobj.objType = DO_TRANSFORM;
		transforminfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		transforminfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&transforminfo[i].dobj);
		transforminfo[i].trftype = atooid(PQgetvalue(res, i, i_trftype));
		transforminfo[i].trflang = atooid(PQgetvalue(res, i, i_trflang));
		transforminfo[i].trffromsql = atooid(PQgetvalue(res, i, i_trffromsql));
		transforminfo[i].trftosql = atooid(PQgetvalue(res, i, i_trftosql));

		/*
		 * Try to name transform as concatenation of type and language name.
		 * This is only used for purposes of sorting.  If we fail to find
		 * either, the name will be an empty string.
		 */
		initPQExpBuffer(&namebuf);
		typeInfo = findTypeByOid(transforminfo[i].trftype);
		lanname = get_language_name(fout, transforminfo[i].trflang);
		if (typeInfo && lanname)
			appendPQExpBuffer(&namebuf, "%s %s",
							  typeInfo->dobj.name, lanname);
		transforminfo[i].dobj.name = namebuf.data;
		free(lanname);

		/* Decide whether we want to dump it */
		selectDumpableObject(&(transforminfo[i].dobj), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return transforminfo;
}

/*
 * getTableAttrs -
 *	  for each interesting table, read info about its attributes
 *	  (names, types, default values, CHECK constraints, etc)
 *
 *	modifies tblinfo
 */
void
getTableAttrs(Archive *fout, TableInfo *tblinfo, int numTables)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q = createPQExpBuffer();
	PQExpBuffer tbloids = createPQExpBuffer();
	PQExpBuffer checkoids = createPQExpBuffer();
	PGresult   *res;
	int			ntups;
	int			curtblindx;
	int			i_attrelid;
	int			i_attnum;
	int			i_attname;
	int			i_atttypname;
	int			i_attstattarget;
	int			i_attstorage;
	int			i_typstorage;
	int			i_attidentity;
	int			i_attgenerated;
	int			i_attisdropped;
	int			i_attlen;
	int			i_attalign;
	int			i_attislocal;
	int			i_attnotnull;
	int			i_attoptions;
	int			i_attcollation;
	int			i_attcompression;
	int			i_attfdwoptions;
	int			i_attmissingval;
	int			i_atthasdef;

	/*
	 * We want to perform just one query against pg_attribute, and then just
	 * one against pg_attrdef (for DEFAULTs) and one against pg_constraint
	 * (for CHECK constraints).  However, we mustn't try to select every row
	 * of those catalogs and then sort it out on the client side, because some
	 * of the server-side functions we need would be unsafe to apply to tables
	 * we don't have lock on.  Hence, we build an array of the OIDs of tables
	 * we care about (and now have lock on!), and use a WHERE clause to
	 * constrain which rows are selected.
	 */
	appendPQExpBufferChar(tbloids, '{');
	appendPQExpBufferChar(checkoids, '{');
	for (int i = 0; i < numTables; i++)
	{
		TableInfo  *tbinfo = &tblinfo[i];

		/* Don't bother to collect info for sequences */
		if (tbinfo->relkind == RELKIND_SEQUENCE)
			continue;

		/* Don't bother with uninteresting tables, either */
		if (!tbinfo->interesting)
			continue;

		/* OK, we need info for this table */
		if (tbloids->len > 1)	/* do we have more than the '{'? */
			appendPQExpBufferChar(tbloids, ',');
		appendPQExpBuffer(tbloids, "%u", tbinfo->dobj.catId.oid);

		if (tbinfo->ncheck > 0)
		{
			/* Also make a list of the ones with check constraints */
			if (checkoids->len > 1) /* do we have more than the '{'? */
				appendPQExpBufferChar(checkoids, ',');
			appendPQExpBuffer(checkoids, "%u", tbinfo->dobj.catId.oid);
		}
	}
	appendPQExpBufferChar(tbloids, '}');
	appendPQExpBufferChar(checkoids, '}');

	/*
	 * Find all the user attributes and their types.
	 *
	 * Since we only want to dump COLLATE clauses for attributes whose
	 * collation is different from their type's default, we use a CASE here to
	 * suppress uninteresting attcollations cheaply.
	 */
	appendPQExpBufferStr(q,
						 "SELECT\n"
						 "a.attrelid,\n"
						 "a.attnum,\n"
						 "a.attname,\n"
						 "a.attstattarget,\n"
						 "a.attstorage,\n"
						 "t.typstorage,\n"
						 "a.attnotnull,\n"
						 "a.atthasdef,\n"
						 "a.attisdropped,\n"
						 "a.attlen,\n"
						 "a.attalign,\n"
						 "a.attislocal,\n"
						 "pg_catalog.format_type(t.oid, a.atttypmod) AS atttypname,\n"
						 "array_to_string(a.attoptions, ', ') AS attoptions,\n"
						 "CASE WHEN a.attcollation <> t.typcollation "
						 "THEN a.attcollation ELSE 0 END AS attcollation,\n"
						 "pg_catalog.array_to_string(ARRAY("
						 "SELECT pg_catalog.quote_ident(option_name) || "
						 "' ' || pg_catalog.quote_literal(option_value) "
						 "FROM pg_catalog.pg_options_to_table(attfdwoptions) "
						 "ORDER BY option_name"
						 "), E',\n    ') AS attfdwoptions,\n");

	if (fout->remoteVersion >= 140000)
		appendPQExpBufferStr(q,
							 "a.attcompression AS attcompression,\n");
	else
		appendPQExpBufferStr(q,
							 "'' AS attcompression,\n");

	if (fout->remoteVersion >= 100000)
		appendPQExpBufferStr(q,
							 "a.attidentity,\n");
	else
		appendPQExpBufferStr(q,
							 "'' AS attidentity,\n");

	if (fout->remoteVersion >= 110000)
		appendPQExpBufferStr(q,
							 "CASE WHEN a.atthasmissing AND NOT a.attisdropped "
							 "THEN a.attmissingval ELSE null END AS attmissingval,\n");
	else
		appendPQExpBufferStr(q,
							 "NULL AS attmissingval,\n");

	if (fout->remoteVersion >= 120000)
		appendPQExpBufferStr(q,
							 "a.attgenerated\n");
	else
		appendPQExpBufferStr(q,
							 "'' AS attgenerated\n");

	/* need left join to pg_type to not fail on dropped columns ... */
	appendPQExpBuffer(q,
					  "FROM unnest('%s'::pg_catalog.oid[]) AS src(tbloid)\n"
					  "JOIN pg_catalog.pg_attribute a ON (src.tbloid = a.attrelid) "
					  "LEFT JOIN pg_catalog.pg_type t "
					  "ON (a.atttypid = t.oid)\n"
					  "WHERE a.attnum > 0::pg_catalog.int2\n"
					  "ORDER BY a.attrelid, a.attnum",
					  tbloids->data);

	res = ExecuteSqlQuery(fout, q->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_attrelid = PQfnumber(res, "attrelid");
	i_attnum = PQfnumber(res, "attnum");
	i_attname = PQfnumber(res, "attname");
	i_atttypname = PQfnumber(res, "atttypname");
	i_attstattarget = PQfnumber(res, "attstattarget");
	i_attstorage = PQfnumber(res, "attstorage");
	i_typstorage = PQfnumber(res, "typstorage");
	i_attidentity = PQfnumber(res, "attidentity");
	i_attgenerated = PQfnumber(res, "attgenerated");
	i_attisdropped = PQfnumber(res, "attisdropped");
	i_attlen = PQfnumber(res, "attlen");
	i_attalign = PQfnumber(res, "attalign");
	i_attislocal = PQfnumber(res, "attislocal");
	i_attnotnull = PQfnumber(res, "attnotnull");
	i_attoptions = PQfnumber(res, "attoptions");
	i_attcollation = PQfnumber(res, "attcollation");
	i_attcompression = PQfnumber(res, "attcompression");
	i_attfdwoptions = PQfnumber(res, "attfdwoptions");
	i_attmissingval = PQfnumber(res, "attmissingval");
	i_atthasdef = PQfnumber(res, "atthasdef");

	/* Within the next loop, we'll accumulate OIDs of tables with defaults */
	resetPQExpBuffer(tbloids);
	appendPQExpBufferChar(tbloids, '{');

	/*
	 * Outer loop iterates once per table, not once per row.  Incrementing of
	 * r is handled by the inner loop.
	 */
	curtblindx = -1;
	for (int r = 0; r < ntups;)
	{
		Oid			attrelid = atooid(PQgetvalue(res, r, i_attrelid));
		TableInfo  *tbinfo = NULL;
		int			numatts;
		bool		hasdefaults;

		/* Count rows for this table */
		for (numatts = 1; numatts < ntups - r; numatts++)
			if (atooid(PQgetvalue(res, r + numatts, i_attrelid)) != attrelid)
				break;

		/*
		 * Locate the associated TableInfo; we rely on tblinfo[] being in OID
		 * order.
		 */
		while (++curtblindx < numTables)
		{
			tbinfo = &tblinfo[curtblindx];
			if (tbinfo->dobj.catId.oid == attrelid)
				break;
		}
		if (curtblindx >= numTables)
			pg_fatal("unrecognized table OID %u", attrelid);
		/* cross-check that we only got requested tables */
		if (tbinfo->relkind == RELKIND_SEQUENCE ||
			!tbinfo->interesting)
			pg_fatal("unexpected column data for table \"%s\"",
					 tbinfo->dobj.name);

		/* Save data for this table */
		tbinfo->numatts = numatts;
		tbinfo->attnames = (char **) pg_malloc(numatts * sizeof(char *));
		tbinfo->atttypnames = (char **) pg_malloc(numatts * sizeof(char *));
		tbinfo->attstattarget = (int *) pg_malloc(numatts * sizeof(int));
		tbinfo->attstorage = (char *) pg_malloc(numatts * sizeof(char));
		tbinfo->typstorage = (char *) pg_malloc(numatts * sizeof(char));
		tbinfo->attidentity = (char *) pg_malloc(numatts * sizeof(char));
		tbinfo->attgenerated = (char *) pg_malloc(numatts * sizeof(char));
		tbinfo->attisdropped = (bool *) pg_malloc(numatts * sizeof(bool));
		tbinfo->attlen = (int *) pg_malloc(numatts * sizeof(int));
		tbinfo->attalign = (char *) pg_malloc(numatts * sizeof(char));
		tbinfo->attislocal = (bool *) pg_malloc(numatts * sizeof(bool));
		tbinfo->attoptions = (char **) pg_malloc(numatts * sizeof(char *));
		tbinfo->attcollation = (Oid *) pg_malloc(numatts * sizeof(Oid));
		tbinfo->attcompression = (char *) pg_malloc(numatts * sizeof(char));
		tbinfo->attfdwoptions = (char **) pg_malloc(numatts * sizeof(char *));
		tbinfo->attmissingval = (char **) pg_malloc(numatts * sizeof(char *));
		tbinfo->notnull = (bool *) pg_malloc(numatts * sizeof(bool));
		tbinfo->inhNotNull = (bool *) pg_malloc(numatts * sizeof(bool));
		tbinfo->attrdefs = (AttrDefInfo **) pg_malloc(numatts * sizeof(AttrDefInfo *));
		hasdefaults = false;

		for (int j = 0; j < numatts; j++, r++)
		{
			if (j + 1 != atoi(PQgetvalue(res, r, i_attnum)))
				pg_fatal("invalid column numbering in table \"%s\"",
						 tbinfo->dobj.name);
			tbinfo->attnames[j] = pg_strdup(PQgetvalue(res, r, i_attname));
			tbinfo->atttypnames[j] = pg_strdup(PQgetvalue(res, r, i_atttypname));
			if (PQgetisnull(res, r, i_attstattarget))
				tbinfo->attstattarget[j] = -1;
			else
				tbinfo->attstattarget[j] = atoi(PQgetvalue(res, r, i_attstattarget));
			tbinfo->attstorage[j] = *(PQgetvalue(res, r, i_attstorage));
			tbinfo->typstorage[j] = *(PQgetvalue(res, r, i_typstorage));
			tbinfo->attidentity[j] = *(PQgetvalue(res, r, i_attidentity));
			tbinfo->attgenerated[j] = *(PQgetvalue(res, r, i_attgenerated));
			tbinfo->needs_override = tbinfo->needs_override || (tbinfo->attidentity[j] == ATTRIBUTE_IDENTITY_ALWAYS);
			tbinfo->attisdropped[j] = (PQgetvalue(res, r, i_attisdropped)[0] == 't');
			tbinfo->attlen[j] = atoi(PQgetvalue(res, r, i_attlen));
			tbinfo->attalign[j] = *(PQgetvalue(res, r, i_attalign));
			tbinfo->attislocal[j] = (PQgetvalue(res, r, i_attislocal)[0] == 't');
			tbinfo->notnull[j] = (PQgetvalue(res, r, i_attnotnull)[0] == 't');
			tbinfo->attoptions[j] = pg_strdup(PQgetvalue(res, r, i_attoptions));
			tbinfo->attcollation[j] = atooid(PQgetvalue(res, r, i_attcollation));
			tbinfo->attcompression[j] = *(PQgetvalue(res, r, i_attcompression));
			tbinfo->attfdwoptions[j] = pg_strdup(PQgetvalue(res, r, i_attfdwoptions));
			tbinfo->attmissingval[j] = pg_strdup(PQgetvalue(res, r, i_attmissingval));
			tbinfo->attrdefs[j] = NULL; /* fix below */
			if (PQgetvalue(res, r, i_atthasdef)[0] == 't')
				hasdefaults = true;
			/* these flags will be set in flagInhAttrs() */
			tbinfo->inhNotNull[j] = false;
		}

		if (hasdefaults)
		{
			/* Collect OIDs of interesting tables that have defaults */
			if (tbloids->len > 1)	/* do we have more than the '{'? */
				appendPQExpBufferChar(tbloids, ',');
			appendPQExpBuffer(tbloids, "%u", tbinfo->dobj.catId.oid);
		}
	}

	PQclear(res);

	/*
	 * Now get info about column defaults.  This is skipped for a data-only
	 * dump, as it is only needed for table schemas.
	 */
	if (!dopt->dataOnly && tbloids->len > 1)
	{
		AttrDefInfo *attrdefs;
		int			numDefaults;
		TableInfo  *tbinfo = NULL;

		pg_log_info("finding table default expressions");

		appendPQExpBufferChar(tbloids, '}');

		printfPQExpBuffer(q, "SELECT a.tableoid, a.oid, adrelid, adnum, "
						  "pg_catalog.pg_get_expr(adbin, adrelid) AS adsrc\n"
						  "FROM unnest('%s'::pg_catalog.oid[]) AS src(tbloid)\n"
						  "JOIN pg_catalog.pg_attrdef a ON (src.tbloid = a.adrelid)\n"
						  "ORDER BY a.adrelid, a.adnum",
						  tbloids->data);

		res = ExecuteSqlQuery(fout, q->data, PGRES_TUPLES_OK);

		numDefaults = PQntuples(res);
		attrdefs = (AttrDefInfo *) pg_malloc(numDefaults * sizeof(AttrDefInfo));

		curtblindx = -1;
		for (int j = 0; j < numDefaults; j++)
		{
			Oid			adtableoid = atooid(PQgetvalue(res, j, 0));
			Oid			adoid = atooid(PQgetvalue(res, j, 1));
			Oid			adrelid = atooid(PQgetvalue(res, j, 2));
			int			adnum = atoi(PQgetvalue(res, j, 3));
			char	   *adsrc = PQgetvalue(res, j, 4);

			/*
			 * Locate the associated TableInfo; we rely on tblinfo[] being in
			 * OID order.
			 */
			if (tbinfo == NULL || tbinfo->dobj.catId.oid != adrelid)
			{
				while (++curtblindx < numTables)
				{
					tbinfo = &tblinfo[curtblindx];
					if (tbinfo->dobj.catId.oid == adrelid)
						break;
				}
				if (curtblindx >= numTables)
					pg_fatal("unrecognized table OID %u", adrelid);
			}

			if (adnum <= 0 || adnum > tbinfo->numatts)
				pg_fatal("invalid adnum value %d for table \"%s\"",
						 adnum, tbinfo->dobj.name);

			/*
			 * dropped columns shouldn't have defaults, but just in case,
			 * ignore 'em
			 */
			if (tbinfo->attisdropped[adnum - 1])
				continue;

			attrdefs[j].dobj.objType = DO_ATTRDEF;
			attrdefs[j].dobj.catId.tableoid = adtableoid;
			attrdefs[j].dobj.catId.oid = adoid;
			AssignDumpId(&attrdefs[j].dobj);
			attrdefs[j].adtable = tbinfo;
			attrdefs[j].adnum = adnum;
			attrdefs[j].adef_expr = pg_strdup(adsrc);

			attrdefs[j].dobj.name = pg_strdup(tbinfo->dobj.name);
			attrdefs[j].dobj.namespace = tbinfo->dobj.namespace;

			attrdefs[j].dobj.dump = tbinfo->dobj.dump;

			/*
			 * Figure out whether the default/generation expression should be
			 * dumped as part of the main CREATE TABLE (or similar) command or
			 * as a separate ALTER TABLE (or similar) command. The preference
			 * is to put it into the CREATE command, but in some cases that's
			 * not possible.
			 */
			if (tbinfo->attgenerated[adnum - 1])
			{
				/*
				 * Column generation expressions cannot be dumped separately,
				 * because there is no syntax for it.  By setting separate to
				 * false here we prevent the "default" from being processed as
				 * its own dumpable object.  Later, flagInhAttrs() will mark
				 * it as not to be dumped at all, if possible (that is, if it
				 * can be inherited from a parent).
				 */
				attrdefs[j].separate = false;
			}
			else if (tbinfo->relkind == RELKIND_VIEW)
			{
				/*
				 * Defaults on a VIEW must always be dumped as separate ALTER
				 * TABLE commands.
				 */
				attrdefs[j].separate = true;
			}
			else if (!shouldPrintColumn(dopt, tbinfo, adnum - 1))
			{
				/* column will be suppressed, print default separately */
				attrdefs[j].separate = true;
			}
			else
			{
				attrdefs[j].separate = false;
			}

			if (!attrdefs[j].separate)
			{
				/*
				 * Mark the default as needing to appear before the table, so
				 * that any dependencies it has must be emitted before the
				 * CREATE TABLE.  If this is not possible, we'll change to
				 * "separate" mode while sorting dependencies.
				 */
				addObjectDependency(&tbinfo->dobj,
									attrdefs[j].dobj.dumpId);
			}

			tbinfo->attrdefs[adnum - 1] = &attrdefs[j];
		}

		PQclear(res);
	}

	/*
	 * Get info about table CHECK constraints.  This is skipped for a
	 * data-only dump, as it is only needed for table schemas.
	 */
	if (!dopt->dataOnly && checkoids->len > 2)
	{
		ConstraintInfo *constrs;
		int			numConstrs;
		int			i_tableoid;
		int			i_oid;
		int			i_conrelid;
		int			i_conname;
		int			i_consrc;
		int			i_conislocal;
		int			i_convalidated;

		pg_log_info("finding table check constraints");

		resetPQExpBuffer(q);
		appendPQExpBuffer(q,
						  "SELECT c.tableoid, c.oid, conrelid, conname, "
						  "pg_catalog.pg_get_constraintdef(c.oid) AS consrc, "
						  "conislocal, convalidated "
						  "FROM unnest('%s'::pg_catalog.oid[]) AS src(tbloid)\n"
						  "JOIN pg_catalog.pg_constraint c ON (src.tbloid = c.conrelid)\n"
						  "WHERE contype = 'c' "
						  "ORDER BY c.conrelid, c.conname",
						  checkoids->data);

		res = ExecuteSqlQuery(fout, q->data, PGRES_TUPLES_OK);

		numConstrs = PQntuples(res);
		constrs = (ConstraintInfo *) pg_malloc(numConstrs * sizeof(ConstraintInfo));

		i_tableoid = PQfnumber(res, "tableoid");
		i_oid = PQfnumber(res, "oid");
		i_conrelid = PQfnumber(res, "conrelid");
		i_conname = PQfnumber(res, "conname");
		i_consrc = PQfnumber(res, "consrc");
		i_conislocal = PQfnumber(res, "conislocal");
		i_convalidated = PQfnumber(res, "convalidated");

		/* As above, this loop iterates once per table, not once per row */
		curtblindx = -1;
		for (int j = 0; j < numConstrs;)
		{
			Oid			conrelid = atooid(PQgetvalue(res, j, i_conrelid));
			TableInfo  *tbinfo = NULL;
			int			numcons;

			/* Count rows for this table */
			for (numcons = 1; numcons < numConstrs - j; numcons++)
				if (atooid(PQgetvalue(res, j + numcons, i_conrelid)) != conrelid)
					break;

			/*
			 * Locate the associated TableInfo; we rely on tblinfo[] being in
			 * OID order.
			 */
			while (++curtblindx < numTables)
			{
				tbinfo = &tblinfo[curtblindx];
				if (tbinfo->dobj.catId.oid == conrelid)
					break;
			}
			if (curtblindx >= numTables)
				pg_fatal("unrecognized table OID %u", conrelid);

			if (numcons != tbinfo->ncheck)
			{
				pg_log_error(ngettext("expected %d check constraint on table \"%s\" but found %d",
									  "expected %d check constraints on table \"%s\" but found %d",
									  tbinfo->ncheck),
							 tbinfo->ncheck, tbinfo->dobj.name, numcons);
				pg_log_error_hint("The system catalogs might be corrupted.");
				exit(1);
			}

			tbinfo->checkexprs = constrs + j;

			for (int c = 0; c < numcons; c++, j++)
			{
				bool		validated = PQgetvalue(res, j, i_convalidated)[0] == 't';

				constrs[j].dobj.objType = DO_CONSTRAINT;
				constrs[j].dobj.catId.tableoid = atooid(PQgetvalue(res, j, i_tableoid));
				constrs[j].dobj.catId.oid = atooid(PQgetvalue(res, j, i_oid));
				AssignDumpId(&constrs[j].dobj);
				constrs[j].dobj.name = pg_strdup(PQgetvalue(res, j, i_conname));
				constrs[j].dobj.namespace = tbinfo->dobj.namespace;
				constrs[j].contable = tbinfo;
				constrs[j].condomain = NULL;
				constrs[j].contype = 'c';
				constrs[j].condef = pg_strdup(PQgetvalue(res, j, i_consrc));
				constrs[j].confrelid = InvalidOid;
				constrs[j].conindex = 0;
				constrs[j].condeferrable = false;
				constrs[j].condeferred = false;
				constrs[j].conislocal = (PQgetvalue(res, j, i_conislocal)[0] == 't');

				/*
				 * An unvalidated constraint needs to be dumped separately, so
				 * that potentially-violating existing data is loaded before
				 * the constraint.
				 */
				constrs[j].separate = !validated;

				constrs[j].dobj.dump = tbinfo->dobj.dump;

				/*
				 * Mark the constraint as needing to appear before the table
				 * --- this is so that any other dependencies of the
				 * constraint will be emitted before we try to create the
				 * table.  If the constraint is to be dumped separately, it
				 * will be dumped after data is loaded anyway, so don't do it.
				 * (There's an automatic dependency in the opposite direction
				 * anyway, so don't need to add one manually here.)
				 */
				if (!constrs[j].separate)
					addObjectDependency(&tbinfo->dobj,
										constrs[j].dobj.dumpId);

				/*
				 * We will detect later whether the constraint must be split
				 * out from the table definition.
				 */
			}
		}

		PQclear(res);
	}

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(tbloids);
	destroyPQExpBuffer(checkoids);
}

/*
 * Test whether a column should be printed as part of table's CREATE TABLE.
 * Column number is zero-based.
 *
 * Normally this is always true, but it's false for dropped columns, as well
 * as those that were inherited without any local definition.  (If we print
 * such a column it will mistakenly get pg_attribute.attislocal set to true.)
 * For partitions, it's always true, because we want the partitions to be
 * created independently and ATTACH PARTITION used afterwards.
 *
 * In binary_upgrade mode, we must print all columns and fix the attislocal/
 * attisdropped state later, so as to keep control of the physical column
 * order.
 *
 * This function exists because there are scattered nonobvious places that
 * must be kept in sync with this decision.
 */
bool
shouldPrintColumn(const DumpOptions *dopt, const TableInfo *tbinfo, int colno)
{
	if (dopt->binary_upgrade)
		return true;
	if (tbinfo->attisdropped[colno])
		return false;
	return (tbinfo->attislocal[colno] || tbinfo->ispartition);
}


/*
 * getTSParsers:
 *	  read all text search parsers in the system catalogs and return them
 *	  in the TSParserInfo* structure
 *
 *	numTSParsers is set to the number of parsers read in
 */
TSParserInfo *
getTSParsers(Archive *fout, int *numTSParsers)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	TSParserInfo *prsinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_prsname;
	int			i_prsnamespace;
	int			i_prsstart;
	int			i_prstoken;
	int			i_prsend;
	int			i_prsheadline;
	int			i_prslextype;

	query = createPQExpBuffer();

	/*
	 * find all text search objects, including builtin ones; we filter out
	 * system-defined objects at dump-out time.
	 */

	appendPQExpBufferStr(query, "SELECT tableoid, oid, prsname, prsnamespace, "
						 "prsstart::oid, prstoken::oid, "
						 "prsend::oid, prsheadline::oid, prslextype::oid "
						 "FROM pg_ts_parser");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numTSParsers = ntups;

	prsinfo = (TSParserInfo *) pg_malloc(ntups * sizeof(TSParserInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_prsname = PQfnumber(res, "prsname");
	i_prsnamespace = PQfnumber(res, "prsnamespace");
	i_prsstart = PQfnumber(res, "prsstart");
	i_prstoken = PQfnumber(res, "prstoken");
	i_prsend = PQfnumber(res, "prsend");
	i_prsheadline = PQfnumber(res, "prsheadline");
	i_prslextype = PQfnumber(res, "prslextype");

	for (i = 0; i < ntups; i++)
	{
		prsinfo[i].dobj.objType = DO_TSPARSER;
		prsinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		prsinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&prsinfo[i].dobj);
		prsinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_prsname));
		prsinfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_prsnamespace)));
		prsinfo[i].prsstart = atooid(PQgetvalue(res, i, i_prsstart));
		prsinfo[i].prstoken = atooid(PQgetvalue(res, i, i_prstoken));
		prsinfo[i].prsend = atooid(PQgetvalue(res, i, i_prsend));
		prsinfo[i].prsheadline = atooid(PQgetvalue(res, i, i_prsheadline));
		prsinfo[i].prslextype = atooid(PQgetvalue(res, i, i_prslextype));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(prsinfo[i].dobj), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return prsinfo;
}

/*
 * getTSDictionaries:
 *	  read all text search dictionaries in the system catalogs and return them
 *	  in the TSDictInfo* structure
 *
 *	numTSDicts is set to the number of dictionaries read in
 */
TSDictInfo *
getTSDictionaries(Archive *fout, int *numTSDicts)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	TSDictInfo *dictinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_dictname;
	int			i_dictnamespace;
	int			i_dictowner;
	int			i_dicttemplate;
	int			i_dictinitoption;

	query = createPQExpBuffer();

	appendPQExpBufferStr(query, "SELECT tableoid, oid, dictname, "
						 "dictnamespace, dictowner, "
						 "dicttemplate, dictinitoption "
						 "FROM pg_ts_dict");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numTSDicts = ntups;

	dictinfo = (TSDictInfo *) pg_malloc(ntups * sizeof(TSDictInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_dictname = PQfnumber(res, "dictname");
	i_dictnamespace = PQfnumber(res, "dictnamespace");
	i_dictowner = PQfnumber(res, "dictowner");
	i_dictinitoption = PQfnumber(res, "dictinitoption");
	i_dicttemplate = PQfnumber(res, "dicttemplate");

	for (i = 0; i < ntups; i++)
	{
		dictinfo[i].dobj.objType = DO_TSDICT;
		dictinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		dictinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&dictinfo[i].dobj);
		dictinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_dictname));
		dictinfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_dictnamespace)));
		dictinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_dictowner));
		dictinfo[i].dicttemplate = atooid(PQgetvalue(res, i, i_dicttemplate));
		if (PQgetisnull(res, i, i_dictinitoption))
			dictinfo[i].dictinitoption = NULL;
		else
			dictinfo[i].dictinitoption = pg_strdup(PQgetvalue(res, i, i_dictinitoption));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(dictinfo[i].dobj), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return dictinfo;
}

/*
 * getTSTemplates:
 *	  read all text search templates in the system catalogs and return them
 *	  in the TSTemplateInfo* structure
 *
 *	numTSTemplates is set to the number of templates read in
 */
TSTemplateInfo *
getTSTemplates(Archive *fout, int *numTSTemplates)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	TSTemplateInfo *tmplinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_tmplname;
	int			i_tmplnamespace;
	int			i_tmplinit;
	int			i_tmpllexize;

	query = createPQExpBuffer();

	appendPQExpBufferStr(query, "SELECT tableoid, oid, tmplname, "
						 "tmplnamespace, tmplinit::oid, tmpllexize::oid "
						 "FROM pg_ts_template");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numTSTemplates = ntups;

	tmplinfo = (TSTemplateInfo *) pg_malloc(ntups * sizeof(TSTemplateInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_tmplname = PQfnumber(res, "tmplname");
	i_tmplnamespace = PQfnumber(res, "tmplnamespace");
	i_tmplinit = PQfnumber(res, "tmplinit");
	i_tmpllexize = PQfnumber(res, "tmpllexize");

	for (i = 0; i < ntups; i++)
	{
		tmplinfo[i].dobj.objType = DO_TSTEMPLATE;
		tmplinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		tmplinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&tmplinfo[i].dobj);
		tmplinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_tmplname));
		tmplinfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_tmplnamespace)));
		tmplinfo[i].tmplinit = atooid(PQgetvalue(res, i, i_tmplinit));
		tmplinfo[i].tmpllexize = atooid(PQgetvalue(res, i, i_tmpllexize));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(tmplinfo[i].dobj), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return tmplinfo;
}

/*
 * getTSConfigurations:
 *	  read all text search configurations in the system catalogs and return
 *	  them in the TSConfigInfo* structure
 *
 *	numTSConfigs is set to the number of configurations read in
 */
TSConfigInfo *
getTSConfigurations(Archive *fout, int *numTSConfigs)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	TSConfigInfo *cfginfo;
	int			i_tableoid;
	int			i_oid;
	int			i_cfgname;
	int			i_cfgnamespace;
	int			i_cfgowner;
	int			i_cfgparser;

	query = createPQExpBuffer();

	appendPQExpBufferStr(query, "SELECT tableoid, oid, cfgname, "
						 "cfgnamespace, cfgowner, cfgparser "
						 "FROM pg_ts_config");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numTSConfigs = ntups;

	cfginfo = (TSConfigInfo *) pg_malloc(ntups * sizeof(TSConfigInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_cfgname = PQfnumber(res, "cfgname");
	i_cfgnamespace = PQfnumber(res, "cfgnamespace");
	i_cfgowner = PQfnumber(res, "cfgowner");
	i_cfgparser = PQfnumber(res, "cfgparser");

	for (i = 0; i < ntups; i++)
	{
		cfginfo[i].dobj.objType = DO_TSCONFIG;
		cfginfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		cfginfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&cfginfo[i].dobj);
		cfginfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_cfgname));
		cfginfo[i].dobj.namespace =
			findNamespace(atooid(PQgetvalue(res, i, i_cfgnamespace)));
		cfginfo[i].rolname = getRoleName(PQgetvalue(res, i, i_cfgowner));
		cfginfo[i].cfgparser = atooid(PQgetvalue(res, i, i_cfgparser));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(cfginfo[i].dobj), fout);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return cfginfo;
}

/*
 * getForeignDataWrappers:
 *	  read all foreign-data wrappers in the system catalogs and return
 *	  them in the FdwInfo* structure
 *
 *	numForeignDataWrappers is set to the number of fdws read in
 */
FdwInfo *
getForeignDataWrappers(Archive *fout, int *numForeignDataWrappers)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	FdwInfo    *fdwinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_fdwname;
	int			i_fdwowner;
	int			i_fdwhandler;
	int			i_fdwvalidator;
	int			i_fdwacl;
	int			i_acldefault;
	int			i_fdwoptions;

	query = createPQExpBuffer();

	appendPQExpBufferStr(query, "SELECT tableoid, oid, fdwname, "
						 "fdwowner, "
						 "fdwhandler::pg_catalog.regproc, "
						 "fdwvalidator::pg_catalog.regproc, "
						 "fdwacl, "
						 "acldefault('F', fdwowner) AS acldefault, "
						 "array_to_string(ARRAY("
						 "SELECT quote_ident(option_name) || ' ' || "
						 "quote_literal(option_value) "
						 "FROM pg_options_to_table(fdwoptions) "
						 "ORDER BY option_name"
						 "), E',\n    ') AS fdwoptions "
						 "FROM pg_foreign_data_wrapper");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numForeignDataWrappers = ntups;

	fdwinfo = (FdwInfo *) pg_malloc(ntups * sizeof(FdwInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_fdwname = PQfnumber(res, "fdwname");
	i_fdwowner = PQfnumber(res, "fdwowner");
	i_fdwhandler = PQfnumber(res, "fdwhandler");
	i_fdwvalidator = PQfnumber(res, "fdwvalidator");
	i_fdwacl = PQfnumber(res, "fdwacl");
	i_acldefault = PQfnumber(res, "acldefault");
	i_fdwoptions = PQfnumber(res, "fdwoptions");

	for (i = 0; i < ntups; i++)
	{
		fdwinfo[i].dobj.objType = DO_FDW;
		fdwinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		fdwinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&fdwinfo[i].dobj);
		fdwinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_fdwname));
		fdwinfo[i].dobj.namespace = NULL;
		fdwinfo[i].dacl.acl = pg_strdup(PQgetvalue(res, i, i_fdwacl));
		fdwinfo[i].dacl.acldefault = pg_strdup(PQgetvalue(res, i, i_acldefault));
		fdwinfo[i].dacl.privtype = 0;
		fdwinfo[i].dacl.initprivs = NULL;
		fdwinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_fdwowner));
		fdwinfo[i].fdwhandler = pg_strdup(PQgetvalue(res, i, i_fdwhandler));
		fdwinfo[i].fdwvalidator = pg_strdup(PQgetvalue(res, i, i_fdwvalidator));
		fdwinfo[i].fdwoptions = pg_strdup(PQgetvalue(res, i, i_fdwoptions));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(fdwinfo[i].dobj), fout);

		/* Mark whether FDW has an ACL */
		if (!PQgetisnull(res, i, i_fdwacl))
			fdwinfo[i].dobj.components |= DUMP_COMPONENT_ACL;
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return fdwinfo;
}

/*
 * getForeignServers:
 *	  read all foreign servers in the system catalogs and return
 *	  them in the ForeignServerInfo * structure
 *
 *	numForeignServers is set to the number of servers read in
 */
ForeignServerInfo *
getForeignServers(Archive *fout, int *numForeignServers)
{
	PGresult   *res;
	int			ntups;
	int			i;
	PQExpBuffer query;
	ForeignServerInfo *srvinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_srvname;
	int			i_srvowner;
	int			i_srvfdw;
	int			i_srvtype;
	int			i_srvversion;
	int			i_srvacl;
	int			i_acldefault;
	int			i_srvoptions;

	query = createPQExpBuffer();

	appendPQExpBufferStr(query, "SELECT tableoid, oid, srvname, "
						 "srvowner, "
						 "srvfdw, srvtype, srvversion, srvacl, "
						 "acldefault('S', srvowner) AS acldefault, "
						 "array_to_string(ARRAY("
						 "SELECT quote_ident(option_name) || ' ' || "
						 "quote_literal(option_value) "
						 "FROM pg_options_to_table(srvoptions) "
						 "ORDER BY option_name"
						 "), E',\n    ') AS srvoptions "
						 "FROM pg_foreign_server");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numForeignServers = ntups;

	srvinfo = (ForeignServerInfo *) pg_malloc(ntups * sizeof(ForeignServerInfo));

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_srvname = PQfnumber(res, "srvname");
	i_srvowner = PQfnumber(res, "srvowner");
	i_srvfdw = PQfnumber(res, "srvfdw");
	i_srvtype = PQfnumber(res, "srvtype");
	i_srvversion = PQfnumber(res, "srvversion");
	i_srvacl = PQfnumber(res, "srvacl");
	i_acldefault = PQfnumber(res, "acldefault");
	i_srvoptions = PQfnumber(res, "srvoptions");

	for (i = 0; i < ntups; i++)
	{
		srvinfo[i].dobj.objType = DO_FOREIGN_SERVER;
		srvinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		srvinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&srvinfo[i].dobj);
		srvinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_srvname));
		srvinfo[i].dobj.namespace = NULL;
		srvinfo[i].dacl.acl = pg_strdup(PQgetvalue(res, i, i_srvacl));
		srvinfo[i].dacl.acldefault = pg_strdup(PQgetvalue(res, i, i_acldefault));
		srvinfo[i].dacl.privtype = 0;
		srvinfo[i].dacl.initprivs = NULL;
		srvinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_srvowner));
		srvinfo[i].srvfdw = atooid(PQgetvalue(res, i, i_srvfdw));
		srvinfo[i].srvtype = pg_strdup(PQgetvalue(res, i, i_srvtype));
		srvinfo[i].srvversion = pg_strdup(PQgetvalue(res, i, i_srvversion));
		srvinfo[i].srvoptions = pg_strdup(PQgetvalue(res, i, i_srvoptions));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(srvinfo[i].dobj), fout);

		/* Servers have user mappings */
		srvinfo[i].dobj.components |= DUMP_COMPONENT_USERMAP;

		/* Mark whether server has an ACL */
		if (!PQgetisnull(res, i, i_srvacl))
			srvinfo[i].dobj.components |= DUMP_COMPONENT_ACL;
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return srvinfo;
}

/*
 * getDefaultACLs:
 *	  read all default ACL information in the system catalogs and return
 *	  them in the DefaultACLInfo structure
 *
 *	numDefaultACLs is set to the number of ACLs read in
 */
DefaultACLInfo *
getDefaultACLs(Archive *fout, int *numDefaultACLs)
{
	DumpOptions *dopt = fout->dopt;
	DefaultACLInfo *daclinfo;
	PQExpBuffer query;
	PGresult   *res;
	int			i_oid;
	int			i_tableoid;
	int			i_defaclrole;
	int			i_defaclnamespace;
	int			i_defaclobjtype;
	int			i_defaclacl;
	int			i_acldefault;
	int			i,
				ntups;

	query = createPQExpBuffer();

	/*
	 * Global entries (with defaclnamespace=0) replace the hard-wired default
	 * ACL for their object type.  We should dump them as deltas from the
	 * default ACL, since that will be used as a starting point for
	 * interpreting the ALTER DEFAULT PRIVILEGES commands.  On the other hand,
	 * non-global entries can only add privileges not revoke them.  We must
	 * dump those as-is (i.e., as deltas from an empty ACL).
	 *
	 * We can use defaclobjtype as the object type for acldefault(), except
	 * for the case of 'S' (DEFACLOBJ_SEQUENCE) which must be converted to
	 * 's'.
	 */
	appendPQExpBufferStr(query,
						 "SELECT oid, tableoid, "
						 "defaclrole, "
						 "defaclnamespace, "
						 "defaclobjtype, "
						 "defaclacl, "
						 "CASE WHEN defaclnamespace = 0 THEN "
						 "acldefault(CASE WHEN defaclobjtype = 'S' "
						 "THEN 's'::\"char\" ELSE defaclobjtype END, "
						 "defaclrole) ELSE '{}' END AS acldefault "
						 "FROM pg_default_acl");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	*numDefaultACLs = ntups;

	daclinfo = (DefaultACLInfo *) pg_malloc(ntups * sizeof(DefaultACLInfo));

	i_oid = PQfnumber(res, "oid");
	i_tableoid = PQfnumber(res, "tableoid");
	i_defaclrole = PQfnumber(res, "defaclrole");
	i_defaclnamespace = PQfnumber(res, "defaclnamespace");
	i_defaclobjtype = PQfnumber(res, "defaclobjtype");
	i_defaclacl = PQfnumber(res, "defaclacl");
	i_acldefault = PQfnumber(res, "acldefault");

	for (i = 0; i < ntups; i++)
	{
		Oid			nspid = atooid(PQgetvalue(res, i, i_defaclnamespace));

		daclinfo[i].dobj.objType = DO_DEFAULT_ACL;
		daclinfo[i].dobj.catId.tableoid = atooid(PQgetvalue(res, i, i_tableoid));
		daclinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&daclinfo[i].dobj);
		/* cheesy ... is it worth coming up with a better object name? */
		daclinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_defaclobjtype));

		if (nspid != InvalidOid)
			daclinfo[i].dobj.namespace = findNamespace(nspid);
		else
			daclinfo[i].dobj.namespace = NULL;

		daclinfo[i].dacl.acl = pg_strdup(PQgetvalue(res, i, i_defaclacl));
		daclinfo[i].dacl.acldefault = pg_strdup(PQgetvalue(res, i, i_acldefault));
		daclinfo[i].dacl.privtype = 0;
		daclinfo[i].dacl.initprivs = NULL;
		daclinfo[i].defaclrole = getRoleName(PQgetvalue(res, i, i_defaclrole));
		daclinfo[i].defaclobjtype = *(PQgetvalue(res, i, i_defaclobjtype));

		/* Default ACLs are ACLs, of course */
		daclinfo[i].dobj.components |= DUMP_COMPONENT_ACL;

		/* Decide whether we want to dump it */
		selectDumpableDefaultACL(&(daclinfo[i]), dopt);
	}

	PQclear(res);

	destroyPQExpBuffer(query);

	return daclinfo;
}






void
getExtensionMembership(Archive *fout, ExtensionInfo extinfo[],
					   int numExtensions)
{
	PQExpBuffer query;
	PGresult   *res;
	int			ntups,
				i;
	int			i_classid,
				i_objid,
				i_refobjid;
	ExtensionInfo *ext;

	/* Nothing to do if no extensions */
	if (numExtensions == 0)
		return;

	query = createPQExpBuffer();

	/* refclassid constraint is redundant but may speed the search */
	appendPQExpBufferStr(query, "SELECT "
						 "classid, objid, refobjid "
						 "FROM pg_depend "
						 "WHERE refclassid = 'pg_extension'::regclass "
						 "AND deptype = 'e' "
						 "ORDER BY 3");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_classid = PQfnumber(res, "classid");
	i_objid = PQfnumber(res, "objid");
	i_refobjid = PQfnumber(res, "refobjid");

	/*
	 * Since we ordered the SELECT by referenced ID, we can expect that
	 * multiple entries for the same extension will appear together; this
	 * saves on searches.
	 */
	ext = NULL;

	for (i = 0; i < ntups; i++)
	{
		CatalogId	objId;
		Oid			extId;

		objId.tableoid = atooid(PQgetvalue(res, i, i_classid));
		objId.oid = atooid(PQgetvalue(res, i, i_objid));
		extId = atooid(PQgetvalue(res, i, i_refobjid));

		if (ext == NULL ||
			ext->dobj.catId.oid != extId)
			ext = findExtensionByOid(extId);

		if (ext == NULL)
		{
			/* shouldn't happen */
			pg_log_warning("could not find referenced extension %u", extId);
			continue;
		}

		recordExtensionMembership(objId, ext);
	}

	PQclear(res);

	destroyPQExpBuffer(query);
}


void
recordExtensionMembership(CatalogId catId, ExtensionInfo *ext)
{
	CatalogIdMapEntry *entry;
	bool		found;

	/* CatalogId hash table must exist, if we have an ExtensionInfo */
	Assert(catalogIdHash != NULL);

	/* Add reference to CatalogId hash */
	entry = catalogid_insert(catalogIdHash, catId, &found);
	if (!found)
	{
		entry->dobj = NULL;
		entry->ext = NULL;
	}
	Assert(entry->ext == NULL);
	entry->ext = ext;
}


void
processExtensionTables(Archive *fout, ExtensionInfo extinfo[],
					   int numExtensions)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer query;
	PGresult   *res;
	int			ntups,
				i;
	int			i_conrelid,
				i_confrelid;

	/* Nothing to do if no extensions */
	if (numExtensions == 0)
		return;

	/*
	 * Identify extension configuration tables and create TableDataInfo
	 * objects for them, ensuring their data will be dumped even though the
	 * tables themselves won't be.
	 *
	 * Note that we create TableDataInfo objects even in schemaOnly mode, ie,
	 * user data in a configuration table is treated like schema data. This
	 * seems appropriate since system data in a config table would get
	 * reloaded by CREATE EXTENSION.  If the extension is not listed in the
	 * list of extensions to be included, none of its data is dumped.
	 */
	for (i = 0; i < numExtensions; i++)
	{
		ExtensionInfo *curext = &(extinfo[i]);
		char	   *extconfig = curext->extconfig;
		char	   *extcondition = curext->extcondition;
		char	  **extconfigarray = NULL;
		char	  **extconditionarray = NULL;
		int			nconfigitems = 0;
		int			nconditionitems = 0;

		/*
		 * Check if this extension is listed as to include in the dump.  If
		 * not, any table data associated with it is discarded.
		 */
		

		if (strlen(extconfig) != 0 || strlen(extcondition) != 0)
		{
			int			j;

			if (!parsePGArray(extconfig, &extconfigarray, &nconfigitems))
				pg_fatal("could not parse %s array", "extconfig");
			if (!parsePGArray(extcondition, &extconditionarray, &nconditionitems))
				pg_fatal("could not parse %s array", "extcondition");
			if (nconfigitems != nconditionitems)
				pg_fatal("mismatched number of configurations and conditions for extension");

			for (j = 0; j < nconfigitems; j++)
			{
				TableInfo  *configtbl;
				Oid			configtbloid = atooid(extconfigarray[j]);
				bool		dumpobj =
					curext->dobj.dump & DUMP_COMPONENT_DEFINITION;

				configtbl = findTableByOid(configtbloid);
				if (configtbl == NULL)
					continue;

				/*
				 * Tables of not-to-be-dumped extensions shouldn't be dumped
				 * unless the table or its schema is explicitly included
				 */
				if (!(curext->dobj.dump & DUMP_COMPONENT_DEFINITION))
				{
					if (table_include_oids.head != NULL &&
						simple_oid_list_member(&table_include_oids,
											   configtbloid))
						dumpobj = true;

					/* check table's schema explicitly requested */
					if (configtbl->dobj.namespace->dobj.dump &
						DUMP_COMPONENT_DATA)
						dumpobj = true;
				}

				

				if (dumpobj)
				{
					makeTableDataInfo(dopt, configtbl);
					if (configtbl->dataObj != NULL)
					{
						if (strlen(extconditionarray[j]) > 0)
							configtbl->dataObj->filtercond = pg_strdup(extconditionarray[j]);
					}
				}
			}
		}
		if (extconfigarray)
			free(extconfigarray);
		if (extconditionarray)
			free(extconditionarray);
	}

	/*
	 * Now that all the TableDataInfo objects have been created for all the
	 * extensions, check their FK dependencies and register them to try and
	 * dump the data out in an order that they can be restored in.
	 *
	 * Note that this is not a problem for user tables as their FKs are
	 * recreated after the data has been loaded.
	 */

	query = createPQExpBuffer();

	printfPQExpBuffer(query,
					  "SELECT conrelid, confrelid "
					  "FROM pg_constraint "
					  "JOIN pg_depend ON (objid = confrelid) "
					  "WHERE contype = 'f' "
					  "AND refclassid = 'pg_extension'::regclass "
					  "AND classid = 'pg_class'::regclass;");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);
	ntups = PQntuples(res);

	i_conrelid = PQfnumber(res, "conrelid");
	i_confrelid = PQfnumber(res, "confrelid");

	/* Now get the dependencies and register them */
	for (i = 0; i < ntups; i++)
	{
		Oid			conrelid,
					confrelid;
		TableInfo  *reftable,
				   *contable;

		conrelid = atooid(PQgetvalue(res, i, i_conrelid));
		confrelid = atooid(PQgetvalue(res, i, i_confrelid));
		contable = findTableByOid(conrelid);
		reftable = findTableByOid(confrelid);

		if (reftable == NULL ||
			reftable->dataObj == NULL ||
			contable == NULL ||
			contable->dataObj == NULL)
			continue;

		/*
		 * Make referencing TABLE_DATA object depend on the referenced table's
		 * TABLE_DATA object.
		 */
		addObjectDependency(&contable->dataObj->dobj,
							reftable->dataObj->dobj.dumpId);
	}
	PQclear(res);
	destroyPQExpBuffer(query);
}


TableInfo *
findTableByOid(Oid oid)
{
	CatalogId	catId;
	DumpableObject *dobj;

	catId.tableoid = RelationRelationId;
	catId.oid = oid;
	dobj = findObjectByCatalogId(catId);
	Assert(dobj == NULL || dobj->objType == DO_TABLE);
	return (TableInfo *) dobj;
}

DumpableObject *
findObjectByCatalogId(CatalogId catalogId)
{
	CatalogIdMapEntry *entry;

	if (catalogIdHash == NULL)
		return NULL;			/* no objects exist yet */

	entry = catalogid_lookup(catalogIdHash, catalogId);
	if (entry == NULL)
		return NULL;
	return entry->dobj;
}




void
flagInhTables(Archive *fout, TableInfo *tblinfo, int numTables,
			  InhInfo *inhinfo, int numInherits)
{
	TableInfo  *child = NULL;
	TableInfo  *parent = NULL;
	int			i,
				j;

	/*
	 * Set up links from child tables to their parents.
	 *
	 * We used to attempt to skip this work for tables that are not to be
	 * dumped; but the optimizable cases are rare in practice, and setting up
	 * these links in bulk is cheaper than the old way.  (Note in particular
	 * that it's very rare for a child to have more than one parent.)
	 */
	for (i = 0; i < numInherits; i++)
	{
		/*
		 * Skip a hashtable lookup if it's same table as last time.  This is
		 * unlikely for the child, but less so for the parent.  (Maybe we
		 * should ask the backend for a sorted array to make it more likely?
		 * Not clear the sorting effort would be repaid, though.)
		 */
		if (child == NULL ||
			child->dobj.catId.oid != inhinfo[i].inhrelid)
		{
			child = findTableByOid(inhinfo[i].inhrelid);

			/*
			 * If we find no TableInfo, assume the pg_inherits entry is for a
			 * partitioned index, which we don't need to track.
			 */
			if (child == NULL)
				continue;
		}
		if (parent == NULL ||
			parent->dobj.catId.oid != inhinfo[i].inhparent)
		{
			parent = findTableByOid(inhinfo[i].inhparent);
			if (parent == NULL)
				pg_fatal("failed sanity check, parent OID %u of table \"%s\" (OID %u) not found",
						 inhinfo[i].inhparent,
						 child->dobj.name,
						 child->dobj.catId.oid);
		}
		/* Add this parent to the child's list of parents. */
		if (child->numParents > 0)
			child->parents = pg_realloc_array(child->parents,
											  TableInfo *,
											  child->numParents + 1);
		else
			child->parents = pg_malloc_array(TableInfo *, 1);
		child->parents[child->numParents++] = parent;
	}

	/*
	 * Now consider all child tables and mark parents interesting as needed.
	 */
	for (i = 0; i < numTables; i++)
	{
		/*
		 * If needed, mark the parents as interesting for getTableAttrs and
		 * getIndexes.  We only need this for direct parents of dumpable
		 * tables.
		 */
		if (tblinfo[i].dobj.dump)
		{
			int			numParents = tblinfo[i].numParents;
			TableInfo **parents = tblinfo[i].parents;

			for (j = 0; j < numParents; j++)
				parents[j]->interesting = true;
		}

		/* Create TableAttachInfo object if needed */
		if ((tblinfo[i].dobj.dump & DUMP_COMPONENT_DEFINITION) &&
			tblinfo[i].ispartition)
		{
			TableAttachInfo *attachinfo;

			/* With partitions there can only be one parent */
			if (tblinfo[i].numParents != 1)
				pg_fatal("invalid number of parents %d for table \"%s\"",
						 tblinfo[i].numParents,
						 tblinfo[i].dobj.name);

			attachinfo = (TableAttachInfo *) palloc(sizeof(TableAttachInfo));
			attachinfo->dobj.objType = DO_TABLE_ATTACH;
			attachinfo->dobj.catId.tableoid = 0;
			attachinfo->dobj.catId.oid = 0;
			AssignDumpId(&attachinfo->dobj);
			attachinfo->dobj.name = pg_strdup(tblinfo[i].dobj.name);
			attachinfo->dobj.namespace = tblinfo[i].dobj.namespace;
			attachinfo->parentTbl = tblinfo[i].parents[0];
			attachinfo->partitionTbl = &tblinfo[i];

			/*
			 * We must state the DO_TABLE_ATTACH object's dependencies
			 * explicitly, since it will not match anything in pg_depend.
			 *
			 * Give it dependencies on both the partition table and the parent
			 * table, so that it will not be executed till both of those
			 * exist.  (There's no need to care what order those are created
			 * in.)
			 */
			addObjectDependency(&attachinfo->dobj, tblinfo[i].dobj.dumpId);
			addObjectDependency(&attachinfo->dobj, tblinfo[i].parents[0]->dobj.dumpId);
		}
	}
}

void
flagInhIndexes(Archive *fout, TableInfo tblinfo[], int numTables)
{
	int			i,
				j;

	for (i = 0; i < numTables; i++)
	{
		if (!tblinfo[i].ispartition || tblinfo[i].numParents == 0)
			continue;

		Assert(tblinfo[i].numParents == 1);

		for (j = 0; j < tblinfo[i].numIndexes; j++)
		{
			IndxInfo   *index = &(tblinfo[i].indexes[j]);
			IndxInfo   *parentidx;
			IndexAttachInfo *attachinfo;

			if (index->parentidx == 0)
				continue;

			parentidx = findIndexByOid(index->parentidx);
			if (parentidx == NULL)
				continue;

			attachinfo = pg_malloc_object(IndexAttachInfo);

			attachinfo->dobj.objType = DO_INDEX_ATTACH;
			attachinfo->dobj.catId.tableoid = 0;
			attachinfo->dobj.catId.oid = 0;
			AssignDumpId(&attachinfo->dobj);
			attachinfo->dobj.name = pg_strdup(index->dobj.name);
			attachinfo->dobj.namespace = index->indextable->dobj.namespace;
			attachinfo->parentIdx = parentidx;
			attachinfo->partitionIdx = index;

			/*
			 * We must state the DO_INDEX_ATTACH object's dependencies
			 * explicitly, since it will not match anything in pg_depend.
			 *
			 * Give it dependencies on both the partition index and the parent
			 * index, so that it will not be executed till both of those
			 * exist.  (There's no need to care what order those are created
			 * in.)
			 *
			 * In addition, give it dependencies on the indexes' underlying
			 * tables.  This does nothing of great value so far as serial
			 * restore ordering goes, but it ensures that a parallel restore
			 * will not try to run the ATTACH concurrently with other
			 * operations on those tables.
			 */
			addObjectDependency(&attachinfo->dobj, index->dobj.dumpId);
			addObjectDependency(&attachinfo->dobj, parentidx->dobj.dumpId);
			addObjectDependency(&attachinfo->dobj,
								index->indextable->dobj.dumpId);
			addObjectDependency(&attachinfo->dobj,
								parentidx->indextable->dobj.dumpId);

			/* keep track of the list of partitions in the parent index */
			simple_ptr_list_append(&parentidx->partattaches, &attachinfo->dobj);
		}
	}
}

/* flagInhAttrs -
 *	 for each dumpable table in tblinfo, flag its inherited attributes
 *
 * What we need to do here is:
 *
 * - Detect child columns that inherit NOT NULL bits from their parents, so
 *   that we needn't specify that again for the child.
 *
 * - Detect child columns that have DEFAULT NULL when their parents had some
 *   non-null default.  In this case, we make up a dummy AttrDefInfo object so
 *   that we'll correctly emit the necessary DEFAULT NULL clause; otherwise
 *   the backend will apply an inherited default to the column.
 *
 * - Detect child columns that have a generation expression and all their
 *   parents also have the same generation expression, and if so suppress the
 *   child's expression.  The child will inherit the generation expression
 *   automatically, so there's no need to dump it.  This improves the dump's
 *   compatibility with pre-v16 servers, which didn't allow the child's
 *   expression to be given explicitly.  Exceptions: If it's a partition or
 *   we are in binary upgrade mode, we dump such expressions anyway because
 *   in those cases inherited tables are recreated standalone first and then
 *   reattached to the parent.  (See also the logic in dumpTableSchema().)
 *
 * modifies tblinfo
 */
void
flagInhAttrs(Archive *fout, TableInfo *tblinfo, int numTables)
{
	DumpOptions *dopt = fout->dopt;
	int			i,
				j,
				k;

	/*
	 * We scan the tables in OID order, since that's how tblinfo[] is sorted.
	 * Hence we will typically visit parents before their children --- but
	 * that is *not* guaranteed.  Thus this loop must be careful that it does
	 * not alter table properties in a way that could change decisions made at
	 * child tables during other iterations.
	 */
	for (i = 0; i < numTables; i++)
	{
		TableInfo  *tbinfo = &(tblinfo[i]);
		int			numParents;
		TableInfo **parents;

		/* Some kinds never have parents */
		if (tbinfo->relkind == RELKIND_SEQUENCE ||
			tbinfo->relkind == RELKIND_VIEW ||
			tbinfo->relkind == RELKIND_MATVIEW)
			continue;

		/* Don't bother computing anything for non-target tables, either */
		if (!tbinfo->dobj.dump)
			continue;

		numParents = tbinfo->numParents;
		parents = tbinfo->parents;

		if (numParents == 0)
			continue;			/* nothing to see here, move along */

		/* For each column, search for matching column names in parent(s) */
		for (j = 0; j < tbinfo->numatts; j++)
		{
			bool		foundNotNull;	/* Attr was NOT NULL in a parent */
			bool		foundDefault;	/* Found a default in a parent */
			bool		foundSameGenerated; /* Found matching GENERATED */
			bool		foundDiffGenerated; /* Found non-matching GENERATED */

			/* no point in examining dropped columns */
			if (tbinfo->attisdropped[j])
				continue;

			foundNotNull = false;
			foundDefault = false;
			foundSameGenerated = false;
			foundDiffGenerated = false;
			for (k = 0; k < numParents; k++)
			{
				TableInfo  *parent = parents[k];
				int			inhAttrInd;

				inhAttrInd = strInArray(tbinfo->attnames[j],
										parent->attnames,
										parent->numatts);
				if (inhAttrInd >= 0)
				{
					AttrDefInfo *parentDef = parent->attrdefs[inhAttrInd];

					foundNotNull |= parent->notnull[inhAttrInd];
					foundDefault |= (parentDef != NULL &&
									 strcmp(parentDef->adef_expr, "NULL") != 0 &&
									 !parent->attgenerated[inhAttrInd]);
					if (parent->attgenerated[inhAttrInd])
					{
						/* these pointer nullness checks are just paranoia */
						if (parentDef != NULL &&
							tbinfo->attrdefs[j] != NULL &&
							strcmp(parentDef->adef_expr,
								   tbinfo->attrdefs[j]->adef_expr) == 0)
							foundSameGenerated = true;
						else
							foundDiffGenerated = true;
					}
				}
			}

			/* Remember if we found inherited NOT NULL */
			tbinfo->inhNotNull[j] = foundNotNull;

			/*
			 * Manufacture a DEFAULT NULL clause if necessary.  This breaks
			 * the advice given above to avoid changing state that might get
			 * inspected in other loop iterations.  We prevent trouble by
			 * having the foundDefault test above check whether adef_expr is
			 * "NULL", so that it will reach the same conclusion before or
			 * after this is done.
			 */
			if (foundDefault && tbinfo->attrdefs[j] == NULL)
			{
				AttrDefInfo *attrDef;

				attrDef = pg_malloc_object(AttrDefInfo);
				attrDef->dobj.objType = DO_ATTRDEF;
				attrDef->dobj.catId.tableoid = 0;
				attrDef->dobj.catId.oid = 0;
				AssignDumpId(&attrDef->dobj);
				attrDef->dobj.name = pg_strdup(tbinfo->dobj.name);
				attrDef->dobj.namespace = tbinfo->dobj.namespace;
				attrDef->dobj.dump = tbinfo->dobj.dump;

				attrDef->adtable = tbinfo;
				attrDef->adnum = j + 1;
				attrDef->adef_expr = pg_strdup("NULL");

				/* Will column be dumped explicitly? */
				if (shouldPrintColumn(dopt, tbinfo, j))
				{
					attrDef->separate = false;
					/* No dependency needed: NULL cannot have dependencies */
				}
				else
				{
					/* column will be suppressed, print default separately */
					attrDef->separate = true;
					/* ensure it comes out after the table */
					addObjectDependency(&attrDef->dobj,
										tbinfo->dobj.dumpId);
				}

				tbinfo->attrdefs[j] = attrDef;
			}

			/* No need to dump generation expression if it's inheritable */
			if (foundSameGenerated && !foundDiffGenerated &&
				!tbinfo->ispartition && !dopt->binary_upgrade)
				tbinfo->attrdefs[j]->dobj.dump = DUMP_COMPONENT_NONE;
		}
	}
}

/*
 * AssignDumpId
 *		Given a newly-created dumpable object, assign a dump ID,
 *		and enter the object into the lookup tables.
 *
 * The caller is expected to have filled in objType and catId,
 * but not any of the other standard fields of a DumpableObject.
 */
void
AssignDumpId(DumpableObject *dobj)
{
	dobj->dumpId = ++lastDumpId;
	dobj->name = NULL;			/* must be set later */
	dobj->namespace = NULL;		/* may be set later */
	dobj->dump = DUMP_COMPONENT_ALL;	/* default assumption */
	dobj->dump_contains = DUMP_COMPONENT_ALL;	/* default assumption */
	/* All objects have definitions; we may set more components bits later */
	dobj->components = DUMP_COMPONENT_DEFINITION;
	dobj->ext_member = false;	/* default assumption */
	dobj->depends_on_ext = false;	/* default assumption */
	dobj->dependencies = NULL;
	dobj->nDeps = 0;
	dobj->allocDeps = 0;

	/* Add object to dumpIdMap[], enlarging that array if need be */
	while (dobj->dumpId >= allocedDumpIds)
	{
		int			newAlloc;

		if (allocedDumpIds <= 0)
		{
			newAlloc = 256;
			dumpIdMap = pg_malloc_array(DumpableObject *, newAlloc);
		}
		else
		{
			newAlloc = allocedDumpIds * 2;
			dumpIdMap = pg_realloc_array(dumpIdMap, DumpableObject *, newAlloc);
		}
		memset(dumpIdMap + allocedDumpIds, 0,
			   (newAlloc - allocedDumpIds) * sizeof(DumpableObject *));
		allocedDumpIds = newAlloc;
	}
	dumpIdMap[dobj->dumpId] = dobj;

	/* If it has a valid CatalogId, enter it into the hash table */
	if (OidIsValid(dobj->catId.tableoid))
	{
		CatalogIdMapEntry *entry;
		bool		found;

		/* Initialize CatalogId hash table if not done yet */
		if (catalogIdHash == NULL)
			catalogIdHash = catalogid_create(CATALOGIDHASH_INITIAL_SIZE, NULL);

		entry = catalogid_insert(catalogIdHash, dobj->catId, &found);
		if (!found)
		{
			entry->dobj = NULL;
			entry->ext = NULL;
		}
		Assert(entry->dobj == NULL);
		entry->dobj = dobj;
	}
}



void
getPolicies(Archive *fout, TableInfo tblinfo[], int numTables)
{
	PQExpBuffer query;
	PQExpBuffer tbloids;
	PGresult   *res;
	PolicyInfo *polinfo;
	int			i_oid;
	int			i_tableoid;
	int			i_polrelid;
	int			i_polname;
	int			i_polcmd;
	int			i_polpermissive;
	int			i_polroles;
	int			i_polqual;
	int			i_polwithcheck;
	int			i,
				j,
				ntups;

	/* No policies before 9.5 */
	if (fout->remoteVersion < 90500)
		return;

	query = createPQExpBuffer();
	tbloids = createPQExpBuffer();

	/*
	 * Identify tables of interest, and check which ones have RLS enabled.
	 */
	appendPQExpBufferChar(tbloids, '{');
	for (i = 0; i < numTables; i++)
	{
		TableInfo  *tbinfo = &tblinfo[i];

		/* Ignore row security on tables not to be dumped */
		if (!(tbinfo->dobj.dump & DUMP_COMPONENT_POLICY))
			continue;

		/* It can't have RLS or policies if it's not a table */
		if (tbinfo->relkind != RELKIND_RELATION &&
			tbinfo->relkind != RELKIND_PARTITIONED_TABLE)
			continue;

		/* Add it to the list of table OIDs to be probed below */
		if (tbloids->len > 1)	/* do we have more than the '{'? */
			appendPQExpBufferChar(tbloids, ',');
		appendPQExpBuffer(tbloids, "%u", tbinfo->dobj.catId.oid);

		/* Is RLS enabled?  (That's separate from whether it has policies) */
		if (tbinfo->rowsec)
		{
			tbinfo->dobj.components |= DUMP_COMPONENT_POLICY;

			/*
			 * We represent RLS being enabled on a table by creating a
			 * PolicyInfo object with null polname.
			 *
			 * Note: use tableoid 0 so that this object won't be mistaken for
			 * something that pg_depend entries apply to.
			 */
			polinfo = pg_malloc(sizeof(PolicyInfo));
			polinfo->dobj.objType = DO_POLICY;
			polinfo->dobj.catId.tableoid = 0;
			polinfo->dobj.catId.oid = tbinfo->dobj.catId.oid;
			AssignDumpId(&polinfo->dobj);
			polinfo->dobj.namespace = tbinfo->dobj.namespace;
			polinfo->dobj.name = pg_strdup(tbinfo->dobj.name);
			polinfo->poltable = tbinfo;
			polinfo->polname = NULL;
			polinfo->polcmd = '\0';
			polinfo->polpermissive = 0;
			polinfo->polroles = NULL;
			polinfo->polqual = NULL;
			polinfo->polwithcheck = NULL;
		}
	}
	appendPQExpBufferChar(tbloids, '}');

	/*
	 * Now, read all RLS policies belonging to the tables of interest, and
	 * create PolicyInfo objects for them.  (Note that we must filter the
	 * results server-side not locally, because we dare not apply pg_get_expr
	 * to tables we don't have lock on.)
	 */
	pg_log_info("reading row-level security policies");

	printfPQExpBuffer(query,
					  "SELECT pol.oid, pol.tableoid, pol.polrelid, pol.polname, pol.polcmd, ");
	if (fout->remoteVersion >= 100000)
		appendPQExpBufferStr(query, "pol.polpermissive, ");
	else
		appendPQExpBufferStr(query, "'t' as polpermissive, ");
	appendPQExpBuffer(query,
					  "CASE WHEN pol.polroles = '{0}' THEN NULL ELSE "
					  "   pg_catalog.array_to_string(ARRAY(SELECT pg_catalog.quote_ident(rolname) from pg_catalog.pg_roles WHERE oid = ANY(pol.polroles)), ', ') END AS polroles, "
					  "pg_catalog.pg_get_expr(pol.polqual, pol.polrelid) AS polqual, "
					  "pg_catalog.pg_get_expr(pol.polwithcheck, pol.polrelid) AS polwithcheck "
					  "FROM unnest('%s'::pg_catalog.oid[]) AS src(tbloid)\n"
					  "JOIN pg_catalog.pg_policy pol ON (src.tbloid = pol.polrelid)",
					  tbloids->data);

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	if (ntups > 0)
	{
		i_oid = PQfnumber(res, "oid");
		i_tableoid = PQfnumber(res, "tableoid");
		i_polrelid = PQfnumber(res, "polrelid");
		i_polname = PQfnumber(res, "polname");
		i_polcmd = PQfnumber(res, "polcmd");
		i_polpermissive = PQfnumber(res, "polpermissive");
		i_polroles = PQfnumber(res, "polroles");
		i_polqual = PQfnumber(res, "polqual");
		i_polwithcheck = PQfnumber(res, "polwithcheck");

		polinfo = pg_malloc(ntups * sizeof(PolicyInfo));

		for (j = 0; j < ntups; j++)
		{
			Oid			polrelid = atooid(PQgetvalue(res, j, i_polrelid));
			TableInfo  *tbinfo = findTableByOid(polrelid);

			tbinfo->dobj.components |= DUMP_COMPONENT_POLICY;

			polinfo[j].dobj.objType = DO_POLICY;
			polinfo[j].dobj.catId.tableoid =
				atooid(PQgetvalue(res, j, i_tableoid));
			polinfo[j].dobj.catId.oid = atooid(PQgetvalue(res, j, i_oid));
			AssignDumpId(&polinfo[j].dobj);
			polinfo[j].dobj.namespace = tbinfo->dobj.namespace;
			polinfo[j].poltable = tbinfo;
			polinfo[j].polname = pg_strdup(PQgetvalue(res, j, i_polname));
			polinfo[j].dobj.name = pg_strdup(polinfo[j].polname);

			polinfo[j].polcmd = *(PQgetvalue(res, j, i_polcmd));
			polinfo[j].polpermissive = *(PQgetvalue(res, j, i_polpermissive)) == 't';

			if (PQgetisnull(res, j, i_polroles))
				polinfo[j].polroles = NULL;
			else
				polinfo[j].polroles = pg_strdup(PQgetvalue(res, j, i_polroles));

			if (PQgetisnull(res, j, i_polqual))
				polinfo[j].polqual = NULL;
			else
				polinfo[j].polqual = pg_strdup(PQgetvalue(res, j, i_polqual));

			if (PQgetisnull(res, j, i_polwithcheck))
				polinfo[j].polwithcheck = NULL;
			else
				polinfo[j].polwithcheck
					= pg_strdup(PQgetvalue(res, j, i_polwithcheck));
		}
	}

	PQclear(res);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(tbloids);
}



PublicationInfo *
getPublications(Archive *fout, int *numPublications)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer query;
	PGresult   *res;
	PublicationInfo *pubinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_pubname;
	int			i_pubowner;
	int			i_puballtables;
	int			i_pubinsert;
	int			i_pubupdate;
	int			i_pubdelete;
	int			i_pubtruncate;
	int			i_pubviaroot;
	int			i,
				ntups;

	if (dopt->no_publications || fout->remoteVersion < 100000)
	{
		*numPublications = 0;
		return NULL;
	}

	query = createPQExpBuffer();

	resetPQExpBuffer(query);

	/* Get the publications. */
	if (fout->remoteVersion >= 130000)
		appendPQExpBufferStr(query,
							 "SELECT p.tableoid, p.oid, p.pubname, "
							 "p.pubowner, "
							 "p.puballtables, p.pubinsert, p.pubupdate, p.pubdelete, p.pubtruncate, p.pubviaroot "
							 "FROM pg_publication p");
	else if (fout->remoteVersion >= 110000)
		appendPQExpBufferStr(query,
							 "SELECT p.tableoid, p.oid, p.pubname, "
							 "p.pubowner, "
							 "p.puballtables, p.pubinsert, p.pubupdate, p.pubdelete, p.pubtruncate, false AS pubviaroot "
							 "FROM pg_publication p");
	else
		appendPQExpBufferStr(query,
							 "SELECT p.tableoid, p.oid, p.pubname, "
							 "p.pubowner, "
							 "p.puballtables, p.pubinsert, p.pubupdate, p.pubdelete, false AS pubtruncate, false AS pubviaroot "
							 "FROM pg_publication p");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_pubname = PQfnumber(res, "pubname");
	i_pubowner = PQfnumber(res, "pubowner");
	i_puballtables = PQfnumber(res, "puballtables");
	i_pubinsert = PQfnumber(res, "pubinsert");
	i_pubupdate = PQfnumber(res, "pubupdate");
	i_pubdelete = PQfnumber(res, "pubdelete");
	i_pubtruncate = PQfnumber(res, "pubtruncate");
	i_pubviaroot = PQfnumber(res, "pubviaroot");

	pubinfo = pg_malloc(ntups * sizeof(PublicationInfo));

	for (i = 0; i < ntups; i++)
	{
		pubinfo[i].dobj.objType = DO_PUBLICATION;
		pubinfo[i].dobj.catId.tableoid =
			atooid(PQgetvalue(res, i, i_tableoid));
		pubinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&pubinfo[i].dobj);
		pubinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_pubname));
		pubinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_pubowner));
		pubinfo[i].puballtables =
			(strcmp(PQgetvalue(res, i, i_puballtables), "t") == 0);
		pubinfo[i].pubinsert =
			(strcmp(PQgetvalue(res, i, i_pubinsert), "t") == 0);
		pubinfo[i].pubupdate =
			(strcmp(PQgetvalue(res, i, i_pubupdate), "t") == 0);
		pubinfo[i].pubdelete =
			(strcmp(PQgetvalue(res, i, i_pubdelete), "t") == 0);
		pubinfo[i].pubtruncate =
			(strcmp(PQgetvalue(res, i, i_pubtruncate), "t") == 0);
		pubinfo[i].pubviaroot =
			(strcmp(PQgetvalue(res, i, i_pubviaroot), "t") == 0);

		/* Decide whether we want to dump it */
		selectDumpableObject(&(pubinfo[i].dobj), fout);
	}
	PQclear(res);

	destroyPQExpBuffer(query);

	*numPublications = ntups;
	return pubinfo;
}



void
getPublicationNamespaces(Archive *fout)
{
	PQExpBuffer query;
	PGresult   *res;
	PublicationSchemaInfo *pubsinfo;
	DumpOptions *dopt = fout->dopt;
	int			i_tableoid;
	int			i_oid;
	int			i_pnpubid;
	int			i_pnnspid;
	int			i,
				j,
				ntups;

	if (dopt->no_publications || fout->remoteVersion < 150000)
		return;

	query = createPQExpBuffer();

	/* Collect all publication membership info. */
	appendPQExpBufferStr(query,
						 "SELECT tableoid, oid, pnpubid, pnnspid "
						 "FROM pg_catalog.pg_publication_namespace");
	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_pnpubid = PQfnumber(res, "pnpubid");
	i_pnnspid = PQfnumber(res, "pnnspid");

	/* this allocation may be more than we need */
	pubsinfo = pg_malloc(ntups * sizeof(PublicationSchemaInfo));
	j = 0;

	for (i = 0; i < ntups; i++)
	{
		Oid			pnpubid = atooid(PQgetvalue(res, i, i_pnpubid));
		Oid			pnnspid = atooid(PQgetvalue(res, i, i_pnnspid));
		PublicationInfo *pubinfo;
		NamespaceInfo *nspinfo;

		/*
		 * Ignore any entries for which we aren't interested in either the
		 * publication or the rel.
		 */
		pubinfo = findPublicationByOid(pnpubid);
		if (pubinfo == NULL)
			continue;
		nspinfo = findNamespaceByOid(pnnspid);
		if (nspinfo == NULL)
			continue;

		/*
		 * We always dump publication namespaces unless the corresponding
		 * namespace is excluded from the dump.
		 */
		if (nspinfo->dobj.dump == DUMP_COMPONENT_NONE)
			continue;

		/* OK, make a DumpableObject for this relationship */
		pubsinfo[j].dobj.objType = DO_PUBLICATION_TABLE_IN_SCHEMA;
		pubsinfo[j].dobj.catId.tableoid =
			atooid(PQgetvalue(res, i, i_tableoid));
		pubsinfo[j].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&pubsinfo[j].dobj);
		pubsinfo[j].dobj.namespace = nspinfo->dobj.namespace;
		pubsinfo[j].dobj.name = nspinfo->dobj.name;
		pubsinfo[j].publication = pubinfo;
		pubsinfo[j].pubschema = nspinfo;

		/* Decide whether we want to dump it */
		selectDumpablePublicationObject(&(pubsinfo[j].dobj), fout);

		j++;
	}

	PQclear(res);
	destroyPQExpBuffer(query);
}

/*
 * getPublicationTables
 *	  get information about publication membership for dumpable tables.
 */
void
getPublicationTables(Archive *fout, TableInfo tblinfo[], int numTables)
{
	PQExpBuffer query;
	PGresult   *res;
	PublicationRelInfo *pubrinfo;
	DumpOptions *dopt = fout->dopt;
	int			i_tableoid;
	int			i_oid;
	int			i_prpubid;
	int			i_prrelid;
	int			i_prrelqual;
	int			i_prattrs;
	int			i,
				j,
				ntups;

	if (dopt->no_publications || fout->remoteVersion < 100000)
		return;

	query = createPQExpBuffer();

	/* Collect all publication membership info. */
	if (fout->remoteVersion >= 150000)
		appendPQExpBufferStr(query,
							 "SELECT tableoid, oid, prpubid, prrelid, "
							 "pg_catalog.pg_get_expr(prqual, prrelid) AS prrelqual, "
							 "(CASE\n"
							 "  WHEN pr.prattrs IS NOT NULL THEN\n"
							 "    (SELECT array_agg(attname)\n"
							 "       FROM\n"
							 "         pg_catalog.generate_series(0, pg_catalog.array_upper(pr.prattrs::pg_catalog.int2[], 1)) s,\n"
							 "         pg_catalog.pg_attribute\n"
							 "      WHERE attrelid = pr.prrelid AND attnum = prattrs[s])\n"
							 "  ELSE NULL END) prattrs "
							 "FROM pg_catalog.pg_publication_rel pr");
	else
		appendPQExpBufferStr(query,
							 "SELECT tableoid, oid, prpubid, prrelid, "
							 "NULL AS prrelqual, NULL AS prattrs "
							 "FROM pg_catalog.pg_publication_rel");
	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_prpubid = PQfnumber(res, "prpubid");
	i_prrelid = PQfnumber(res, "prrelid");
	i_prrelqual = PQfnumber(res, "prrelqual");
	i_prattrs = PQfnumber(res, "prattrs");

	/* this allocation may be more than we need */
	pubrinfo = pg_malloc(ntups * sizeof(PublicationRelInfo));
	j = 0;

	for (i = 0; i < ntups; i++)
	{
		Oid			prpubid = atooid(PQgetvalue(res, i, i_prpubid));
		Oid			prrelid = atooid(PQgetvalue(res, i, i_prrelid));
		PublicationInfo *pubinfo;
		TableInfo  *tbinfo;

		/*
		 * Ignore any entries for which we aren't interested in either the
		 * publication or the rel.
		 */
		pubinfo = findPublicationByOid(prpubid);
		if (pubinfo == NULL)
			continue;
		tbinfo = findTableByOid(prrelid);
		if (tbinfo == NULL)
			continue;

		/*
		 * Ignore publication membership of tables whose definitions are not
		 * to be dumped.
		 */
		if (!(tbinfo->dobj.dump & DUMP_COMPONENT_DEFINITION))
			continue;

		/* OK, make a DumpableObject for this relationship */
		pubrinfo[j].dobj.objType = DO_PUBLICATION_REL;
		pubrinfo[j].dobj.catId.tableoid =
			atooid(PQgetvalue(res, i, i_tableoid));
		pubrinfo[j].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&pubrinfo[j].dobj);
		pubrinfo[j].dobj.namespace = tbinfo->dobj.namespace;
		pubrinfo[j].dobj.name = tbinfo->dobj.name;
		pubrinfo[j].publication = pubinfo;
		pubrinfo[j].pubtable = tbinfo;
		if (PQgetisnull(res, i, i_prrelqual))
			pubrinfo[j].pubrelqual = NULL;
		else
			pubrinfo[j].pubrelqual = pg_strdup(PQgetvalue(res, i, i_prrelqual));

		if (!PQgetisnull(res, i, i_prattrs))
		{
			char	  **attnames;
			int			nattnames;
			PQExpBuffer attribs;

			if (!parsePGArray(PQgetvalue(res, i, i_prattrs),
							  &attnames, &nattnames))
				pg_fatal("could not parse %s array", "prattrs");
			attribs = createPQExpBuffer();
			for (int k = 0; k < nattnames; k++)
			{
				if (k > 0)
					appendPQExpBufferStr(attribs, ", ");

				appendPQExpBufferStr(attribs, fmtId(attnames[k]));
			}
			pubrinfo[j].pubrattrs = attribs->data;
		}
		else
			pubrinfo[j].pubrattrs = NULL;

		/* Decide whether we want to dump it */
		selectDumpablePublicationObject(&(pubrinfo[j].dobj), fout);

		j++;
	}

	PQclear(res);
	destroyPQExpBuffer(query);
}


void
getSubscriptions(Archive *fout)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer query;
	PGresult   *res;
	SubscriptionInfo *subinfo;
	int			i_tableoid;
	int			i_oid;
	int			i_subname;
	int			i_subowner;
	int			i_subbinary;
	int			i_substream;
	int			i_subtwophasestate;
	int			i_subdisableonerr;
	int			i_subpasswordrequired;
	int			i_subrunasowner;
	int			i_subconninfo;
	int			i_subslotname;
	int			i_subsynccommit;
	int			i_subpublications;
	int			i_suborigin;
	int			i_suboriginremotelsn;
	int			i_subenabled;
	int			i_subfailover;
	int			i,
				ntups;

	if (dopt->no_subscriptions || fout->remoteVersion < 100000)
		return;

	if (!is_superuser(fout))
	{
		int			n;

		res = ExecuteSqlQuery(fout,
							  "SELECT count(*) FROM pg_subscription "
							  "WHERE subdbid = (SELECT oid FROM pg_database"
							  "                 WHERE datname = current_database())",
							  PGRES_TUPLES_OK);
		n = atoi(PQgetvalue(res, 0, 0));
		if (n > 0)
			pg_log_warning("subscriptions not dumped because current user is not a superuser");
		PQclear(res);
		return;
	}

	query = createPQExpBuffer();

	/* Get the subscriptions in current database. */
	appendPQExpBufferStr(query,
						 "SELECT s.tableoid, s.oid, s.subname,\n"
						 " s.subowner,\n"
						 " s.subconninfo, s.subslotname, s.subsynccommit,\n"
						 " s.subpublications,\n");

	if (fout->remoteVersion >= 140000)
		appendPQExpBufferStr(query, " s.subbinary,\n");
	else
		appendPQExpBufferStr(query, " false AS subbinary,\n");

	if (fout->remoteVersion >= 140000)
		appendPQExpBufferStr(query, " s.substream,\n");
	else
		appendPQExpBufferStr(query, " 'f' AS substream,\n");

	if (fout->remoteVersion >= 150000)
		appendPQExpBufferStr(query,
							 " s.subtwophasestate,\n"
							 " s.subdisableonerr,\n");
	else
		appendPQExpBuffer(query,
						  " '%c' AS subtwophasestate,\n"
						  " false AS subdisableonerr,\n",
						  LOGICALREP_TWOPHASE_STATE_DISABLED);

	if (fout->remoteVersion >= 160000)
		appendPQExpBufferStr(query,
							 " s.subpasswordrequired,\n"
							 " s.subrunasowner,\n"
							 " s.suborigin,\n");
	else
		appendPQExpBuffer(query,
						  " 't' AS subpasswordrequired,\n"
						  " 't' AS subrunasowner,\n"
						  " '%s' AS suborigin,\n",
						  LOGICALREP_ORIGIN_ANY);

	if (dopt->binary_upgrade && fout->remoteVersion >= 170000)
		appendPQExpBufferStr(query, " o.remote_lsn AS suboriginremotelsn,\n"
							 " s.subenabled,\n");
	else
		appendPQExpBufferStr(query, " NULL AS suboriginremotelsn,\n"
							 " false AS subenabled,\n");

	if (fout->remoteVersion >= 170000)
		appendPQExpBufferStr(query,
							 " s.subfailover\n");
	else
		appendPQExpBuffer(query,
						  " false AS subfailover\n");

	appendPQExpBufferStr(query,
						 "FROM pg_subscription s\n");

	if (dopt->binary_upgrade && fout->remoteVersion >= 170000)
		appendPQExpBufferStr(query,
							 "LEFT JOIN pg_catalog.pg_replication_origin_status o \n"
							 "    ON o.external_id = 'pg_' || s.oid::text \n");

	appendPQExpBufferStr(query,
						 "WHERE s.subdbid = (SELECT oid FROM pg_database\n"
						 "                   WHERE datname = current_database())");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	/*
	 * Get subscription fields. We don't include subskiplsn in the dump as
	 * after restoring the dump this value may no longer be relevant.
	 */
	i_tableoid = PQfnumber(res, "tableoid");
	i_oid = PQfnumber(res, "oid");
	i_subname = PQfnumber(res, "subname");
	i_subowner = PQfnumber(res, "subowner");
	i_subbinary = PQfnumber(res, "subbinary");
	i_substream = PQfnumber(res, "substream");
	i_subtwophasestate = PQfnumber(res, "subtwophasestate");
	i_subdisableonerr = PQfnumber(res, "subdisableonerr");
	i_subpasswordrequired = PQfnumber(res, "subpasswordrequired");
	i_subrunasowner = PQfnumber(res, "subrunasowner");
	i_subconninfo = PQfnumber(res, "subconninfo");
	i_subslotname = PQfnumber(res, "subslotname");
	i_subsynccommit = PQfnumber(res, "subsynccommit");
	i_subpublications = PQfnumber(res, "subpublications");
	i_suborigin = PQfnumber(res, "suborigin");
	i_suboriginremotelsn = PQfnumber(res, "suboriginremotelsn");
	i_subenabled = PQfnumber(res, "subenabled");
	i_subfailover = PQfnumber(res, "subfailover");

	subinfo = pg_malloc(ntups * sizeof(SubscriptionInfo));

	for (i = 0; i < ntups; i++)
	{
		subinfo[i].dobj.objType = DO_SUBSCRIPTION;
		subinfo[i].dobj.catId.tableoid =
			atooid(PQgetvalue(res, i, i_tableoid));
		subinfo[i].dobj.catId.oid = atooid(PQgetvalue(res, i, i_oid));
		AssignDumpId(&subinfo[i].dobj);
		subinfo[i].dobj.name = pg_strdup(PQgetvalue(res, i, i_subname));
		subinfo[i].rolname = getRoleName(PQgetvalue(res, i, i_subowner));

		subinfo[i].subbinary =
			pg_strdup(PQgetvalue(res, i, i_subbinary));
		subinfo[i].substream =
			pg_strdup(PQgetvalue(res, i, i_substream));
		subinfo[i].subtwophasestate =
			pg_strdup(PQgetvalue(res, i, i_subtwophasestate));
		subinfo[i].subdisableonerr =
			pg_strdup(PQgetvalue(res, i, i_subdisableonerr));
		subinfo[i].subpasswordrequired =
			pg_strdup(PQgetvalue(res, i, i_subpasswordrequired));
		subinfo[i].subrunasowner =
			pg_strdup(PQgetvalue(res, i, i_subrunasowner));
		subinfo[i].subconninfo =
			pg_strdup(PQgetvalue(res, i, i_subconninfo));
		if (PQgetisnull(res, i, i_subslotname))
			subinfo[i].subslotname = NULL;
		else
			subinfo[i].subslotname =
				pg_strdup(PQgetvalue(res, i, i_subslotname));
		subinfo[i].subsynccommit =
			pg_strdup(PQgetvalue(res, i, i_subsynccommit));
		subinfo[i].subpublications =
			pg_strdup(PQgetvalue(res, i, i_subpublications));
		subinfo[i].suborigin = pg_strdup(PQgetvalue(res, i, i_suborigin));
		if (PQgetisnull(res, i, i_suboriginremotelsn))
			subinfo[i].suboriginremotelsn = NULL;
		else
			subinfo[i].suboriginremotelsn =
				pg_strdup(PQgetvalue(res, i, i_suboriginremotelsn));
		subinfo[i].subenabled =
			pg_strdup(PQgetvalue(res, i, i_subenabled));
		subinfo[i].subfailover =
			pg_strdup(PQgetvalue(res, i, i_subfailover));

		/* Decide whether we want to dump it */
		selectDumpableObject(&(subinfo[i].dobj), fout);
	}
	PQclear(res);

	destroyPQExpBuffer(query);
}


void
getSubscriptionTables(Archive *fout)
{
	DumpOptions *dopt = fout->dopt;
	SubscriptionInfo *subinfo = NULL;
	SubRelInfo *subrinfo;
	PGresult   *res;
	int			i_srsubid;
	int			i_srrelid;
	int			i_srsubstate;
	int			i_srsublsn;
	int			ntups;
	Oid			last_srsubid = InvalidOid;

	if (dopt->no_subscriptions || !dopt->binary_upgrade ||
		fout->remoteVersion < 170000)
		return;

	res = ExecuteSqlQuery(fout,
						  "SELECT srsubid, srrelid, srsubstate, srsublsn "
						  "FROM pg_catalog.pg_subscription_rel "
						  "ORDER BY srsubid",
						  PGRES_TUPLES_OK);
	ntups = PQntuples(res);
	if (ntups == 0)
		goto cleanup;

	/* Get pg_subscription_rel attributes */
	i_srsubid = PQfnumber(res, "srsubid");
	i_srrelid = PQfnumber(res, "srrelid");
	i_srsubstate = PQfnumber(res, "srsubstate");
	i_srsublsn = PQfnumber(res, "srsublsn");

	subrinfo = pg_malloc(ntups * sizeof(SubRelInfo));
	for (int i = 0; i < ntups; i++)
	{
		Oid			cur_srsubid = atooid(PQgetvalue(res, i, i_srsubid));
		Oid			relid = atooid(PQgetvalue(res, i, i_srrelid));
		TableInfo  *tblinfo;

		/*
		 * If we switched to a new subscription, check if the subscription
		 * exists.
		 */
		if (cur_srsubid != last_srsubid)
		{
			subinfo = findSubscriptionByOid(cur_srsubid);
			if (subinfo == NULL)
				pg_fatal("subscription with OID %u does not exist", cur_srsubid);

			last_srsubid = cur_srsubid;
		}

		tblinfo = findTableByOid(relid);
		if (tblinfo == NULL)
			pg_fatal("failed sanity check, table with OID %u not found",
					 relid);

		/* OK, make a DumpableObject for this relationship */
		subrinfo[i].dobj.objType = DO_SUBSCRIPTION_REL;
		subrinfo[i].dobj.catId.tableoid = relid;
		subrinfo[i].dobj.catId.oid = cur_srsubid;
		AssignDumpId(&subrinfo[i].dobj);
		subrinfo[i].dobj.name = pg_strdup(subinfo->dobj.name);
		subrinfo[i].tblinfo = tblinfo;
		subrinfo[i].srsubstate = PQgetvalue(res, i, i_srsubstate)[0];
		if (PQgetisnull(res, i, i_srsublsn))
			subrinfo[i].srsublsn = NULL;
		else
			subrinfo[i].srsublsn = pg_strdup(PQgetvalue(res, i, i_srsublsn));

		subrinfo[i].subinfo = subinfo;

		/* Decide whether we want to dump it */
		selectDumpableObject(&(subrinfo[i].dobj), fout);
	}

cleanup:
	PQclear(res);
}


 void
selectDumpableNamespace(NamespaceInfo *nsinfo, Archive *fout)
{
	/*
	 * DUMP_COMPONENT_DEFINITION typically implies a CREATE SCHEMA statement
	 * and (for --clean) a DROP SCHEMA statement.  (In the absence of
	 * DUMP_COMPONENT_DEFINITION, this value is irrelevant.)
	 */
	nsinfo->create = true;

	/*
	 * If specific tables are being dumped, do not dump any complete
	 * namespaces. If specific namespaces are being dumped, dump just those
	 * namespaces. Otherwise, dump all non-system namespaces.
	 */
	if (table_include_oids.head != NULL)
		nsinfo->dobj.dump_contains = nsinfo->dobj.dump = DUMP_COMPONENT_NONE;
	else if (fout->remoteVersion >= 90600 &&
			 strcmp(nsinfo->dobj.name, "pg_catalog") == 0)
	{
		/*
		 * In 9.6 and above, we dump out any ACLs defined in pg_catalog, if
		 * they are interesting (and not the original ACLs which were set at
		 * initdb time, see pg_init_privs).
		 */
		nsinfo->dobj.dump_contains = nsinfo->dobj.dump = DUMP_COMPONENT_ACL;
	}
	else if (strncmp(nsinfo->dobj.name, "pg_", 3) == 0 ||
			 strcmp(nsinfo->dobj.name, "information_schema") == 0)
	{
		/* Other system schemas don't get dumped */
		nsinfo->dobj.dump_contains = nsinfo->dobj.dump = DUMP_COMPONENT_NONE;
	}
	else if (strcmp(nsinfo->dobj.name, "public") == 0)
	{
		/*
		 * The public schema is a strange beast that sits in a sort of
		 * no-mans-land between being a system object and a user object.
		 * CREATE SCHEMA would fail, so its DUMP_COMPONENT_DEFINITION is just
		 * a comment and an indication of ownership.  If the owner is the
		 * default, omit that superfluous DUMP_COMPONENT_DEFINITION.  Before
		 * v15, the default owner was BOOTSTRAP_SUPERUSERID.
		 */
		nsinfo->create = false;
		nsinfo->dobj.dump = DUMP_COMPONENT_ALL;
		if (nsinfo->nspowner == ROLE_PG_DATABASE_OWNER)
			nsinfo->dobj.dump &= ~DUMP_COMPONENT_DEFINITION;
		nsinfo->dobj.dump_contains = DUMP_COMPONENT_ALL;

		/*
		 * Also, make like it has a comment even if it doesn't; this is so
		 * that we'll emit a command to drop the comment, if appropriate.
		 * (Without this, we'd not call dumpCommentExtended for it.)
		 */
		nsinfo->dobj.components |= DUMP_COMPONENT_COMMENT;
	}
	else
		nsinfo->dobj.dump_contains = nsinfo->dobj.dump = DUMP_COMPONENT_ALL;

	

	/*
	 * If the schema belongs to an extension, allow extension membership to
	 * override the dump decision for the schema itself.  However, this does
	 * not change dump_contains, so this won't change what we do with objects
	 * within the schema.  (If they belong to the extension, they'll get
	 * suppressed by it, otherwise not.)
	 */
	(void) checkExtensionMembership(&nsinfo->dobj, fout);
}

/*
 * selectDumpableTable: policy-setting subroutine
 *		Mark a table as to be dumped or not
 */
 void
selectDumpableTable(TableInfo *tbinfo, Archive *fout)
{
	if (checkExtensionMembership(&tbinfo->dobj, fout))
		return;					/* extension membership overrides all else */

	/*
	 * If specific tables are being dumped, dump just those tables; else, dump
	 * according to the parent namespace's dump flag.
	 */
		if (table_include_oids.head != NULL)
			tbinfo->dobj.dump = (simple_oid_list_member(&table_include_oids,
													tbinfo->dobj.catId.oid) || is_in_view_oids(tbinfo->dobj.catId.oid)) ?
				DUMP_COMPONENT_ALL : DUMP_COMPONENT_NONE;
		else	
			tbinfo->dobj.dump = tbinfo->dobj.namespace->dobj.dump_contains;

	/*
	 * In any case, a table can be excluded by an exclusion switch
	 */
	
}

/*
 * selectDumpableType: policy-setting subroutine
 *		Mark a type as to be dumped or not
 *
 * If it's a table's rowtype or an autogenerated array type, we also apply a
 * special type code to facilitate sorting into the desired order.  (We don't
 * want to consider those to be ordinary types because that would bring tables
 * up into the datatype part of the dump order.)  We still set the object's
 * dump flag; that's not going to cause the dummy type to be dumped, but we
 * need it so that casts involving such types will be dumped correctly -- see
 * dumpCast.  This means the flag should be set the same as for the underlying
 * object (the table or base type).
 */
 void
selectDumpableType(TypeInfo *tyinfo, Archive *fout)
{
	/* skip complex types, except for standalone composite types */
	if (OidIsValid(tyinfo->typrelid) &&
		tyinfo->typrelkind != RELKIND_COMPOSITE_TYPE)
	{
		TableInfo  *tytable = findTableByOid(tyinfo->typrelid);

		tyinfo->dobj.objType = DO_DUMMY_TYPE;
		if (tytable != NULL)
			tyinfo->dobj.dump = tytable->dobj.dump;
		else
			tyinfo->dobj.dump = DUMP_COMPONENT_NONE;
		return;
	}

	/* skip auto-generated array and multirange types */
	if (tyinfo->isArray || tyinfo->isMultirange)
	{
		tyinfo->dobj.objType = DO_DUMMY_TYPE;

		/*
		 * Fall through to set the dump flag; we assume that the subsequent
		 * rules will do the same thing as they would for the array's base
		 * type or multirange's range type.  (We cannot reliably look up the
		 * base type here, since getTypes may not have processed it yet.)
		 */
	}

	if (checkExtensionMembership(&tyinfo->dobj, fout))
		return;					/* extension membership overrides all else */

	/* Dump based on if the contents of the namespace are being dumped */
	tyinfo->dobj.dump = tyinfo->dobj.namespace->dobj.dump_contains;
}

/*
 * selectDumpableDefaultACL: policy-setting subroutine
 *		Mark a default ACL as to be dumped or not
 *
 * For per-schema default ACLs, dump if the schema is to be dumped.
 * Otherwise dump if we are dumping "everything".  Note that dataOnly
 * and aclsSkip are checked separately.
 */
 void
selectDumpableDefaultACL(DefaultACLInfo *dinfo, DumpOptions *dopt)
{
	/* Default ACLs can't be extension members */

	if (dinfo->dobj.namespace)
		/* default ACLs are considered part of the namespace */
		dinfo->dobj.dump = dinfo->dobj.namespace->dobj.dump_contains;
	else
		dinfo->dobj.dump = dopt->include_everything ?
			DUMP_COMPONENT_ALL : DUMP_COMPONENT_NONE;
}

/*
 * selectDumpableCast: policy-setting subroutine
 *		Mark a cast as to be dumped or not
 *
 * Casts do not belong to any particular namespace (since they haven't got
 * names), nor do they have identifiable owners.  To distinguish user-defined
 * casts from built-in ones, we must resort to checking whether the cast's
 * OID is in the range reserved for initdb.
 */
 void
selectDumpableCast(CastInfo *cast, Archive *fout)
{
	if (checkExtensionMembership(&cast->dobj, fout))
		return;					/* extension membership overrides all else */

	/*
	 * This would be DUMP_COMPONENT_ACL for from-initdb casts, but they do not
	 * support ACLs currently.
	 */
	if (cast->dobj.catId.oid <= (Oid) g_last_builtin_oid)
		cast->dobj.dump = DUMP_COMPONENT_NONE;
	else
		cast->dobj.dump = DUMP_COMPONENT_NONE;
}

/*
 * selectDumpableProcLang: policy-setting subroutine
 *		Mark a procedural language as to be dumped or not
 *
 * Procedural languages do not belong to any particular namespace.  To
 * identify built-in languages, we must resort to checking whether the
 * language's OID is in the range reserved for initdb.
 */
 void
selectDumpableProcLang(ProcLangInfo *plang, Archive *fout)
{
	if (checkExtensionMembership(&plang->dobj, fout))
		return;					/* extension membership overrides all else */

	/*
	 * Only include procedural languages when we are dumping everything.
	 *
	 * For from-initdb procedural languages, only include ACLs, as we do for
	 * the pg_catalog namespace.  We need this because procedural languages do
	 * not live in any namespace.
	 */
	if (!fout->dopt->include_everything)
		plang->dobj.dump = DUMP_COMPONENT_NONE;
	else
	{
		if (plang->dobj.catId.oid <= (Oid) g_last_builtin_oid)
			plang->dobj.dump = fout->remoteVersion < 90600 ?
				DUMP_COMPONENT_NONE : DUMP_COMPONENT_ACL;
		else
			plang->dobj.dump = DUMP_COMPONENT_ALL;
	}
}

/*
 * selectDumpableAccessMethod: policy-setting subroutine
 *		Mark an access method as to be dumped or not
 *
 * Access methods do not belong to any particular namespace.  To identify
 * built-in access methods, we must resort to checking whether the
 * method's OID is in the range reserved for initdb.
 */
 void
selectDumpableAccessMethod(AccessMethodInfo *method, Archive *fout)
{
	if (checkExtensionMembership(&method->dobj, fout))
		return;					/* extension membership overrides all else */

	/*
	 * This would be DUMP_COMPONENT_ACL for from-initdb access methods, but
	 * they do not support ACLs currently.
	 */
	if (method->dobj.catId.oid <= (Oid) g_last_builtin_oid)
		method->dobj.dump = DUMP_COMPONENT_NONE;
	else
		method->dobj.dump = fout->dopt->include_everything ?
			DUMP_COMPONENT_ALL : DUMP_COMPONENT_NONE;
}

/*
 * selectDumpableExtension: policy-setting subroutine
 *		Mark an extension as to be dumped or not
 *
 * Built-in extensions should be skipped except for checking ACLs, since we
 * assume those will already be installed in the target database.  We identify
 * such extensions by their having OIDs in the range reserved for initdb.
 * We dump all user-added extensions by default.  No extensions are dumped
 * if include_everything is false (i.e., a --schema or --table switch was
 * given), except if --extension specifies a list of extensions to dump.
 */
 void
selectDumpableExtension(ExtensionInfo *extinfo, DumpOptions *dopt)
{
	/*
	 * Use DUMP_COMPONENT_ACL for built-in extensions, to allow users to
	 * change permissions on their member objects, if they wish to, and have
	 * those changes preserved.
	 */
	if (extinfo->dobj.catId.oid <= (Oid) g_last_builtin_oid)
		extinfo->dobj.dump = extinfo->dobj.dump_contains = DUMP_COMPONENT_ACL;
	else
	{
		
			extinfo->dobj.dump = extinfo->dobj.dump_contains =
				dopt->include_everything ?
				DUMP_COMPONENT_ALL : DUMP_COMPONENT_NONE;

		
	}
}

/*
 * selectDumpablePublicationObject: policy-setting subroutine
 *		Mark a publication object as to be dumped or not
 *
 * A publication can have schemas and tables which have schemas, but those are
 * ignored in decision making, because publications are only dumped when we are
 * dumping everything.
 */
 void
selectDumpablePublicationObject(DumpableObject *dobj, Archive *fout)
{
	if (checkExtensionMembership(dobj, fout))
		return;					/* extension membership overrides all else */

	dobj->dump = fout->dopt->include_everything ?
		DUMP_COMPONENT_ALL : DUMP_COMPONENT_NONE;
}

/*
 * selectDumpableStatisticsObject: policy-setting subroutine
 *		Mark an extended statistics object as to be dumped or not
 *
 * We dump an extended statistics object if the schema it's in and the table
 * it's for are being dumped.  (This'll need more thought if statistics
 * objects ever support cross-table stats.)
 */
 void
selectDumpableStatisticsObject(StatsExtInfo *sobj, Archive *fout)
{
	if (checkExtensionMembership(&sobj->dobj, fout))
		return;					/* extension membership overrides all else */

	sobj->dobj.dump = sobj->dobj.namespace->dobj.dump_contains;
	if (sobj->stattable == NULL ||
		!(sobj->stattable->dobj.dump & DUMP_COMPONENT_DEFINITION))
		sobj->dobj.dump = DUMP_COMPONENT_NONE;
}

/*
 * selectDumpableObject: policy-setting subroutine
 *		Mark a generic dumpable object as to be dumped or not
 *
 * Use this only for object types without a special-case routine above.
 */
 void
selectDumpableObject(DumpableObject *dobj, Archive *fout)
{
	if (checkExtensionMembership(dobj, fout))
		return;					/* extension membership overrides all else */

	/*
	 * Default policy is to dump if parent namespace is dumpable, or for
	 * non-namespace-associated items, dump if we're dumping "everything".
	 */
	if (dobj->namespace)
		dobj->dump = dobj->namespace->dobj.dump_contains;
	else
		dobj->dump = fout->dopt->include_everything ?
			DUMP_COMPONENT_ALL : DUMP_COMPONENT_NONE;
}




void
collectRoleNames(Archive *fout)
{
	PGresult   *res;
	const char *query;
	int			i;

	query = "SELECT oid, rolname FROM pg_catalog.pg_roles ORDER BY 1";

	res = ExecuteSqlQuery(fout, query, PGRES_TUPLES_OK);

	nrolenames = PQntuples(res);

	rolenames = (RoleNameItem *) pg_malloc(nrolenames * sizeof(RoleNameItem));

	for (i = 0; i < nrolenames; i++)
	{
		rolenames[i].roleoid = atooid(PQgetvalue(res, i, 0));
		rolenames[i].rolename = pg_strdup(PQgetvalue(res, i, 1));
	}

	PQclear(res);
}

/*
 * getRoleName -- look up the name of a role, given its OID
 *
 * In current usage, we don't expect failures, so error out for a bad OID.
 */
const char *
getRoleName(const char *roleoid_str)
{
	Oid			roleoid = atooid(roleoid_str);

	/*
	 * Do binary search to find the appropriate item.
	 */
	if (nrolenames > 0)
	{
		RoleNameItem *low = &rolenames[0];
		RoleNameItem *high = &rolenames[nrolenames - 1];

		while (low <= high)
		{
			RoleNameItem *middle = low + (high - low) / 2;

			if (roleoid < middle->roleoid)
				high = middle - 1;
			else if (roleoid > middle->roleoid)
				low = middle + 1;
			else
				return middle->rolename;	/* found a match */
		}
	}

	pg_fatal("role with OID %u does not exist", roleoid);
	return NULL;				/* keep compiler quiet */
}


bool
checkExtensionMembership(DumpableObject *dobj, Archive *fout)
{
	ExtensionInfo *ext = findOwningExtension(dobj->catId);

	if (ext == NULL)
		return false;

	dobj->ext_member = true;

	/* Record dependency so that getDependencies needn't deal with that */
	addObjectDependency(dobj, ext->dobj.dumpId);

	/*
	 * In 9.6 and above, mark the member object to have any non-initial ACLs
	 * dumped.  (Any initial ACLs will be removed later, using data from
	 * pg_init_privs, so that we'll dump only the delta from the extension's
	 * initial setup.)
	 *
	 * Prior to 9.6, we do not include any extension member components.
	 *
	 * In binary upgrades, we still dump all components of the members
	 * individually, since the idea is to exactly reproduce the database
	 * contents rather than replace the extension contents with something
	 * different.
	 *
	 * Note: it might be interesting someday to implement storage and delta
	 * dumping of extension members' RLS policies and/or security labels.
	 * However there is a pitfall for RLS policies: trying to dump them
	 * requires getting a lock on their tables, and the calling user might not
	 * have privileges for that.  We need no lock to examine a table's ACLs,
	 * so the current feature doesn't have a problem of that sort.
	 */
	if (fout->dopt->binary_upgrade)
		dobj->dump = ext->dobj.dump;
	else
	{
		if (fout->remoteVersion < 90600)
			dobj->dump = DUMP_COMPONENT_NONE;
		else
			dobj->dump = ext->dobj.dump_contains & (DUMP_COMPONENT_ACL);
	}

	return true;
}


ExtensionInfo *
findOwningExtension(CatalogId catalogId)
{
	CatalogIdMapEntry *entry;

	if (catalogIdHash == NULL)
		return NULL;			/* no objects exist yet */

	entry = catalogid_lookup(catalogIdHash, catalogId);
	if (entry == NULL)
		return NULL;
	return entry->ext;
}

DumpableObject *
createBoundaryObjects(void)
{
	DumpableObject *dobjs;

	dobjs = (DumpableObject *) pg_malloc(2 * sizeof(DumpableObject));

	dobjs[0].objType = DO_PRE_DATA_BOUNDARY;
	dobjs[0].catId = nilCatalogId;
	AssignDumpId(dobjs + 0);
	dobjs[0].name = pg_strdup("PRE-DATA BOUNDARY");

	dobjs[1].objType = DO_POST_DATA_BOUNDARY;
	dobjs[1].catId = nilCatalogId;
	AssignDumpId(dobjs + 1);
	dobjs[1].name = pg_strdup("POST-DATA BOUNDARY");

	return dobjs;
}




static int
DOTypeNameCompare(const void *p1, const void *p2)
{
	DumpableObject *obj1 = *(DumpableObject *const *) p1;
	DumpableObject *obj2 = *(DumpableObject *const *) p2;
	int			cmpval;

	/* Sort by type's priority */
	cmpval = dbObjectTypePriority[obj1->objType] -
		dbObjectTypePriority[obj2->objType];

	if (cmpval != 0)
		return cmpval;

	/*
	 * Sort by namespace.  Typically, all objects of the same priority would
	 * either have or not have a namespace link, but there are exceptions.
	 * Sort NULL namespace after non-NULL in such cases.
	 */
	if (obj1->namespace)
	{
		if (obj2->namespace)
		{
			cmpval = strcmp(obj1->namespace->dobj.name,
							obj2->namespace->dobj.name);
			if (cmpval != 0)
				return cmpval;
		}
		else
			return -1;
	}
	else if (obj2->namespace)
		return 1;

	/* Sort by name */
	cmpval = strcmp(obj1->name, obj2->name);
	if (cmpval != 0)
		return cmpval;

	/* To have a stable sort order, break ties for some object types */
	if (obj1->objType == DO_FUNC || obj1->objType == DO_AGG)
	{
		FuncInfo   *fobj1 = *(FuncInfo *const *) p1;
		FuncInfo   *fobj2 = *(FuncInfo *const *) p2;
		int			i;

		/* Sort by number of arguments, then argument type names */
		cmpval = fobj1->nargs - fobj2->nargs;
		if (cmpval != 0)
			return cmpval;
		for (i = 0; i < fobj1->nargs; i++)
		{
			TypeInfo   *argtype1 = findTypeByOid(fobj1->argtypes[i]);
			TypeInfo   *argtype2 = findTypeByOid(fobj2->argtypes[i]);

			if (argtype1 && argtype2)
			{
				if (argtype1->dobj.namespace && argtype2->dobj.namespace)
				{
					cmpval = strcmp(argtype1->dobj.namespace->dobj.name,
									argtype2->dobj.namespace->dobj.name);
					if (cmpval != 0)
						return cmpval;
				}
				cmpval = strcmp(argtype1->dobj.name, argtype2->dobj.name);
				if (cmpval != 0)
					return cmpval;
			}
		}
	}
	else if (obj1->objType == DO_OPERATOR)
	{
		OprInfo    *oobj1 = *(OprInfo *const *) p1;
		OprInfo    *oobj2 = *(OprInfo *const *) p2;

		/* oprkind is 'l', 'r', or 'b'; this sorts prefix, postfix, infix */
		cmpval = (oobj2->oprkind - oobj1->oprkind);
		if (cmpval != 0)
			return cmpval;
	}
	else if (obj1->objType == DO_ATTRDEF)
	{
		AttrDefInfo *adobj1 = *(AttrDefInfo *const *) p1;
		AttrDefInfo *adobj2 = *(AttrDefInfo *const *) p2;

		/* Sort by attribute number */
		cmpval = (adobj1->adnum - adobj2->adnum);
		if (cmpval != 0)
			return cmpval;
	}
	else if (obj1->objType == DO_POLICY)
	{
		PolicyInfo *pobj1 = *(PolicyInfo *const *) p1;
		PolicyInfo *pobj2 = *(PolicyInfo *const *) p2;

		/* Sort by table name (table namespace was considered already) */
		cmpval = strcmp(pobj1->poltable->dobj.name,
						pobj2->poltable->dobj.name);
		if (cmpval != 0)
			return cmpval;
	}
	else if (obj1->objType == DO_TRIGGER)
	{
		TriggerInfo *tobj1 = *(TriggerInfo *const *) p1;
		TriggerInfo *tobj2 = *(TriggerInfo *const *) p2;

		/* Sort by table name (table namespace was considered already) */
		cmpval = strcmp(tobj1->tgtable->dobj.name,
						tobj2->tgtable->dobj.name);
		if (cmpval != 0)
			return cmpval;
	}

	/* Usually shouldn't get here, but if we do, sort by OID */
	return oidcmp(obj1->catId.oid, obj2->catId.oid);
}

DumpId
getMaxDumpId(void)
{
	return lastDumpId;
}

void
sortDumpableObjectsByTypeName(DumpableObject **objs, int numObjs)
{
	if (numObjs > 1)
		qsort(objs, numObjs, sizeof(DumpableObject *),
			  DOTypeNameCompare);
}


void
getDumpableObjects(DumpableObject ***objs, int *numObjs)
{
	int			i,
				j;

	*objs = pg_malloc_array(DumpableObject *, allocedDumpIds);
	j = 0;
	for (i = 1; i < allocedDumpIds; i++)
	{
		if (dumpIdMap[i])
			(*objs)[j++] = dumpIdMap[i];
	}
	*numObjs = j;
}

DumpableObject *
findObjectByDumpId(DumpId dumpId)
{
	if (dumpId <= 0 || dumpId >= allocedDumpIds)
		return NULL;			/* out of range? */
	return dumpIdMap[dumpId];
}


DumpId
createDumpId(void)
{
	return ++lastDumpId;
}




TocEntry *
ArchiveEntry(Archive *AHX, CatalogId catalogId, DumpId dumpId,
			 ArchiveOpts *opts)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;
	TocEntry   *newToc;

	newToc = (TocEntry *) pg_malloc0(sizeof(TocEntry));

	AH->tocCount++;
	if (dumpId > AH->maxDumpId)
		AH->maxDumpId = dumpId;

	newToc->prev = AH->toc->prev;
	newToc->next = AH->toc;
	AH->toc->prev->next = newToc;
	AH->toc->prev = newToc;

	newToc->catalogId = catalogId;
	newToc->dumpId = dumpId;
	newToc->section = opts->section;

	newToc->tag = pg_strdup(opts->tag);
	newToc->namespace = opts->namespace ? pg_strdup(opts->namespace) : NULL;
	newToc->tablespace = opts->tablespace ? pg_strdup(opts->tablespace) : NULL;
	newToc->tableam = opts->tableam ? pg_strdup(opts->tableam) : NULL;
	newToc->relkind = opts->relkind;
	newToc->owner = opts->owner ? pg_strdup(opts->owner) : NULL;
	newToc->desc = pg_strdup(opts->description);
	newToc->defn = opts->createStmt ? pg_strdup(opts->createStmt) : NULL;
	newToc->dropStmt = opts->dropStmt ? pg_strdup(opts->dropStmt) : NULL;
	newToc->copyStmt = opts->copyStmt ? pg_strdup(opts->copyStmt) : NULL;

	if (opts->nDeps > 0)
	{
		newToc->dependencies = (DumpId *) pg_malloc(opts->nDeps * sizeof(DumpId));
		memcpy(newToc->dependencies, opts->deps, opts->nDeps * sizeof(DumpId));
		newToc->nDeps = opts->nDeps;
	}
	else
	{
		newToc->dependencies = NULL;
		newToc->nDeps = 0;
	}

	newToc->dataDumper = opts->dumpFn;
	newToc->dataDumperArg = opts->dumpArg;
	newToc->hadDumper = opts->dumpFn ? true : false;

	newToc->formatData = NULL;
	newToc->dataLength = 0;

	if (AH->ArchiveEntryPtr != NULL)
		AH->ArchiveEntryPtr(AH, newToc);

	return newToc;
}