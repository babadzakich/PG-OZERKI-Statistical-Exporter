#include "pg_ozerki.h"


void dumpDumpableObject(Archive *fout, DumpableObject *dobj);
static void dumpNamespace(Archive *fout, const NamespaceInfo *nspinfo);
static void dumpExtension(Archive *fout, const ExtensionInfo *extinfo);
static void dumpType(Archive *fout, const TypeInfo *tyinfo);
static void dumpBaseType(Archive *fout, const TypeInfo *tyinfo);
static void dumpEnumType(Archive *fout, const TypeInfo *tyinfo);
static int	findComments(Oid classoid, Oid objoid, CommentItem **items);
static void dumpRangeType(Archive *fout, const TypeInfo *tyinfo);
static void dumpUndefinedType(Archive *fout, const TypeInfo *tyinfo);
static void dumpDomain(Archive *fout, const TypeInfo *tyinfo);
static void dumpCompositeType(Archive *fout, const TypeInfo *tyinfo);
static void dumpCompositeTypeColComments(Archive *fout, const TypeInfo *tyinfo,
										 PGresult *res);
static void dumpShellType(Archive *fout, const ShellTypeInfo *stinfo);
static void dumpProcLang(Archive *fout, const ProcLangInfo *plang);
static void dumpFunc(Archive *fout, const FuncInfo *finfo);
static void dumpCast(Archive *fout, const CastInfo *cast);
static void dumpTransform(Archive *fout, const TransformInfo *transform);
static void dumpOpr(Archive *fout, const OprInfo *oprinfo);
static void dumpAccessMethod(Archive *fout, const AccessMethodInfo *aminfo);
static void dumpOpclass(Archive *fout, const OpclassInfo *opcinfo);
static void dumpOpfamily(Archive *fout, const OpfamilyInfo *opfinfo);
static void dumpCollation(Archive *fout, const CollInfo *collinfo);
static void dumpConversion(Archive *fout, const ConvInfo *convinfo);
static void dumpRule(Archive *fout, const RuleInfo *rinfo);
static void dumpAgg(Archive *fout, const AggInfo *agginfo);
static void dumpTrigger(Archive *fout, const TriggerInfo *tginfo);
static void
dumpPolicy(Archive *fout, const PolicyInfo *polinfo);
static void dumpEventTrigger(Archive *fout, const EventTriggerInfo *evtinfo);
static void dumpTable(Archive *fout, const TableInfo *tbinfo);
static void dumpTableSchema(Archive *fout, const TableInfo *tbinfo);
static void dumpTableAttach(Archive *fout, const TableAttachInfo *attachinfo);
static void dumpAttrDef(Archive *fout, const AttrDefInfo *adinfo);
static void dumpSequence(Archive *fout, const TableInfo *tbinfo);
static void dumpSequenceData(Archive *fout, const TableDataInfo *tdinfo);
static void dumpIndex(Archive *fout, const IndxInfo *indxinfo);
static void dumpIndexAttach(Archive *fout, const IndexAttachInfo *attachinfo);
static void dumpStatisticsExt(Archive *fout, const StatsExtInfo *statsextinfo);
static void dumpConstraint(Archive *fout, const ConstraintInfo *coninfo);
static void dumpTableConstraintComment(Archive *fout, const ConstraintInfo *coninfo);
static void dumpTSParser(Archive *fout, const TSParserInfo *prsinfo);
static void dumpTSDictionary(Archive *fout, const TSDictInfo *dictinfo);
static void dumpTSTemplate(Archive *fout, const TSTemplateInfo *tmplinfo);
static void dumpTSConfig(Archive *fout, const TSConfigInfo *cfginfo);
static void dumpForeignDataWrapper(Archive *fout, const FdwInfo *fdwinfo);
static void
dumpPublicationNamespace(Archive *fout, const PublicationSchemaInfo *pubsinfo);
static void dumpForeignServer(Archive *fout, const ForeignServerInfo *srvinfo);
static bool nonemptyReloptions(const char *reloptions);
static void
dumpPublicationTable(Archive *fout, const PublicationRelInfo *pubrinfo);
static void
dumpSubscription(Archive *fout, const SubscriptionInfo *subinfo);
static void
dumpPublication(Archive *fout, const PublicationInfo *pubinfo);
static void
dumpSubscriptionTable(Archive *fout, const SubRelInfo *subrinfo);
static void
refreshMatViewData(Archive *fout, const TableDataInfo *tdinfo);

static void dumpUserMappings(Archive *fout,
							 const char *servername, const char *namespace,
							 const char *owner, CatalogId catalogId, DumpId dumpId);
static void dumpDefaultACL(Archive *fout, const DefaultACLInfo *daclinfo);
static void appendReloptionsArrayAH(PQExpBuffer buffer, const char *reloptions,
									const char *prefix, Archive *fout);

static const char *getFormattedTypeName(Archive *fout, Oid oid, OidOptions opts);

static DumpId dumpACL(Archive *fout, DumpId objDumpId, DumpId altDumpId,
					  const char *type, const char *name, const char *subname,
					  const char *nspname, const char *tag, const char *owner,
					  const DumpableAcl *dacl);


static void
dumpSecLabel(Archive *fout, const char *type, const char *name,
			 const char *namespace, const char *owner,
			 CatalogId catalogId, int subid, DumpId dumpId);

static void
dumpCommentExtended(Archive *fout, const char *type,
					const char *name, const char *namespace,
					const char *owner, CatalogId catalogId,
					int subid, DumpId dumpId,
					const char *initdb_comment);


static inline void dumpComment(Archive *fout, const char *type,
							   const char *name, const char *namespace,
							   const char *owner, CatalogId catalogId,
							   int subid, DumpId dumpId);


static void
dumpTableComment(Archive *fout, const TableInfo *tbinfo,
				 const char *reltypename);

static const char *
getAttrName(int attrnum, const TableInfo *tblInfo);
static int
findSecLabels(Oid classoid, Oid objoid, SecLabelItem **items);

static char *
getFormattedOperatorName(const char *oproid);

static char *
convertRegProcReference(const char *proc);

static SecLabelItem *seclabels = NULL;
static int	nseclabels = 0;

static CommentItem *comments = NULL;
static int	ncomments = 0;


static bool
nonemptyReloptions(const char *reloptions)
{
	/* Don't want to print it if it's just "{}" */
	return (reloptions != NULL && strlen(reloptions) > 2);
}

static void
dumpSubscriptionTable(Archive *fout, const SubRelInfo *subrinfo)
{
	DumpOptions *dopt = fout->dopt;
	SubscriptionInfo *subinfo = subrinfo->subinfo;
	PQExpBuffer query;
	char	   *tag;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	Assert(fout->dopt->binary_upgrade && fout->remoteVersion >= 170000);

	tag = psprintf("%s %s", subinfo->dobj.name, subrinfo->dobj.name);

	query = createPQExpBuffer();

	if (subinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
	{
		/*
		 * binary_upgrade_add_sub_rel_state will add the subscription relation
		 * to pg_subscription_rel table. This will be used only in
		 * binary-upgrade mode.
		 */
		appendPQExpBufferStr(query,
							 "\n-- For binary upgrade, must preserve the subscriber table.\n");
		appendPQExpBufferStr(query,
							 "SELECT pg_catalog.binary_upgrade_add_sub_rel_state(");
		appendStringLiteralAH(query, subrinfo->dobj.name, fout);
		appendPQExpBuffer(query,
						  ", %u, '%c'",
						  subrinfo->tblinfo->dobj.catId.oid,
						  subrinfo->srsubstate);

		if (subrinfo->srsublsn && subrinfo->srsublsn[0] != '\0')
			appendPQExpBuffer(query, ", '%s'", subrinfo->srsublsn);
		else
			appendPQExpBuffer(query, ", NULL");

		appendPQExpBufferStr(query, ");\n");
	}

	/*
	 * There is no point in creating a drop query as the drop is done by table
	 * drop.  (If you think to change this, see also _printTocEntry().)
	 * Although this object doesn't really have ownership as such, set the
	 * owner field anyway to ensure that the command is run by the correct
	 * role at restore time.
	 */
	if (subrinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, subrinfo->dobj.catId, subrinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tag,
								  .namespace = subrinfo->tblinfo->dobj.namespace->dobj.name,
								  .owner = subinfo->rolname,
								  .description = "SUBSCRIPTION TABLE",
								  .section = SECTION_POST_DATA,
								  .createStmt = query->data));

	/* These objects can't currently have comments or seclabels */

	free(tag);
	destroyPQExpBuffer(query);
}
static void
dumpPublicationNamespace(Archive *fout, const PublicationSchemaInfo *pubsinfo)
{
	DumpOptions *dopt = fout->dopt;
	NamespaceInfo *schemainfo = pubsinfo->pubschema;
	PublicationInfo *pubinfo = pubsinfo->publication;
	PQExpBuffer query;
	char	   *tag;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	tag = psprintf("%s %s", pubinfo->dobj.name, schemainfo->dobj.name);

	query = createPQExpBuffer();

	appendPQExpBuffer(query, "ALTER PUBLICATION %s ", fmtId(pubinfo->dobj.name));
	appendPQExpBuffer(query, "ADD TABLES IN SCHEMA %s;\n", fmtId(schemainfo->dobj.name));

	/*
	 * There is no point in creating drop query as the drop is done by schema
	 * drop.
	 */
	if (pubsinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, pubsinfo->dobj.catId, pubsinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tag,
								  .namespace = schemainfo->dobj.name,
								  .owner = pubinfo->rolname,
								  .description = "PUBLICATION TABLES IN SCHEMA",
								  .section = SECTION_POST_DATA,
								  .createStmt = query->data));

	/* These objects can't currently have comments or seclabels */

	free(tag);
	destroyPQExpBuffer(query);
}

static void
dumpPublicationTable(Archive *fout, const PublicationRelInfo *pubrinfo)
{
	DumpOptions *dopt = fout->dopt;
	PublicationInfo *pubinfo = pubrinfo->publication;
	TableInfo  *tbinfo = pubrinfo->pubtable;
	PQExpBuffer query;
	char	   *tag;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	tag = psprintf("%s %s", pubinfo->dobj.name, tbinfo->dobj.name);

	query = createPQExpBuffer();

	appendPQExpBuffer(query, "ALTER PUBLICATION %s ADD TABLE ONLY",
					  fmtId(pubinfo->dobj.name));
	appendPQExpBuffer(query, " %s",
					  fmtQualifiedDumpable(tbinfo));

	if (pubrinfo->pubrattrs)
		appendPQExpBuffer(query, " (%s)", pubrinfo->pubrattrs);

	if (pubrinfo->pubrelqual)
	{
		/*
		 * It's necessary to add parentheses around the expression because
		 * pg_get_expr won't supply the parentheses for things like WHERE
		 * TRUE.
		 */
		appendPQExpBuffer(query, " WHERE (%s)", pubrinfo->pubrelqual);
	}
	appendPQExpBufferStr(query, ";\n");

	/*
	 * There is no point in creating a drop query as the drop is done by table
	 * drop.  (If you think to change this, see also _printTocEntry().)
	 * Although this object doesn't really have ownership as such, set the
	 * owner field anyway to ensure that the command is run by the correct
	 * role at restore time.
	 */
	if (pubrinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, pubrinfo->dobj.catId, pubrinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tag,
								  .namespace = tbinfo->dobj.namespace->dobj.name,
								  .owner = pubinfo->rolname,
								  .description = "PUBLICATION TABLE",
								  .section = SECTION_POST_DATA,
								  .createStmt = query->data));

	/* These objects can't currently have comments or seclabels */

	free(tag);
	destroyPQExpBuffer(query);
}

static void
dumpSubscription(Archive *fout, const SubscriptionInfo *subinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer delq;
	PQExpBuffer query;
	PQExpBuffer publications;
	char	   *qsubname;
	char	  **pubnames = NULL;
	int			npubnames = 0;
	int			i;
	char		two_phase_disabled[] = {LOGICALREP_TWOPHASE_STATE_DISABLED, '\0'};

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	delq = createPQExpBuffer();
	query = createPQExpBuffer();

	qsubname = pg_strdup(fmtId(subinfo->dobj.name));

	appendPQExpBuffer(delq, "DROP SUBSCRIPTION %s;\n",
					  qsubname);

	appendPQExpBuffer(query, "CREATE SUBSCRIPTION %s CONNECTION ",
					  qsubname);
	appendStringLiteralAH(query, subinfo->subconninfo, fout);

	/* Build list of quoted publications and append them to query. */
	if (!parsePGArray(subinfo->subpublications, &pubnames, &npubnames))
		pg_fatal("could not parse %s array", "subpublications");

	publications = createPQExpBuffer();
	for (i = 0; i < npubnames; i++)
	{
		if (i > 0)
			appendPQExpBufferStr(publications, ", ");

		appendPQExpBufferStr(publications, fmtId(pubnames[i]));
	}

	appendPQExpBuffer(query, " PUBLICATION %s WITH (connect = false, slot_name = ", publications->data);
	if (subinfo->subslotname)
		appendStringLiteralAH(query, subinfo->subslotname, fout);
	else
		appendPQExpBufferStr(query, "NONE");

	if (strcmp(subinfo->subbinary, "t") == 0)
		appendPQExpBufferStr(query, ", binary = true");

	if (strcmp(subinfo->substream, "t") == 0)
		appendPQExpBufferStr(query, ", streaming = on");
	else if (strcmp(subinfo->substream, "p") == 0)
		appendPQExpBufferStr(query, ", streaming = parallel");

	if (strcmp(subinfo->subtwophasestate, two_phase_disabled) != 0)
		appendPQExpBufferStr(query, ", two_phase = on");

	if (strcmp(subinfo->subdisableonerr, "t") == 0)
		appendPQExpBufferStr(query, ", disable_on_error = true");

	if (strcmp(subinfo->subpasswordrequired, "t") != 0)
		appendPQExpBuffer(query, ", password_required = false");

	if (strcmp(subinfo->subrunasowner, "t") == 0)
		appendPQExpBufferStr(query, ", run_as_owner = true");

	if (strcmp(subinfo->subfailover, "t") == 0)
		appendPQExpBufferStr(query, ", failover = true");

	if (strcmp(subinfo->subsynccommit, "off") != 0)
		appendPQExpBuffer(query, ", synchronous_commit = %s", fmtId(subinfo->subsynccommit));

	if (pg_strcasecmp(subinfo->suborigin, LOGICALREP_ORIGIN_ANY) != 0)
		appendPQExpBuffer(query, ", origin = %s", subinfo->suborigin);

	appendPQExpBufferStr(query, ");\n");

	/*
	 * In binary-upgrade mode, we allow the replication to continue after the
	 * upgrade.
	 */
	if (dopt->binary_upgrade && fout->remoteVersion >= 170000)
	{
		if (subinfo->suboriginremotelsn)
		{
			/*
			 * Preserve the remote_lsn for the subscriber's replication
			 * origin. This value is required to start the replication from
			 * the position before the upgrade. This value will be stale if
			 * the publisher gets upgraded before the subscriber node.
			 * However, this shouldn't be a problem as the upgrade of the
			 * publisher ensures that all the transactions were replicated
			 * before upgrading it.
			 */
			appendPQExpBufferStr(query,
								 "\n-- For binary upgrade, must preserve the remote_lsn for the subscriber's replication origin.\n");
			appendPQExpBufferStr(query,
								 "SELECT pg_catalog.binary_upgrade_replorigin_advance(");
			appendStringLiteralAH(query, subinfo->dobj.name, fout);
			appendPQExpBuffer(query, ", '%s');\n", subinfo->suboriginremotelsn);
		}

		if (strcmp(subinfo->subenabled, "t") == 0)
		{
			/*
			 * Enable the subscription to allow the replication to continue
			 * after the upgrade.
			 */
			appendPQExpBufferStr(query,
								 "\n-- For binary upgrade, must preserve the subscriber's running state.\n");
			appendPQExpBuffer(query, "ALTER SUBSCRIPTION %s ENABLE;\n", qsubname);
		}
	}

	if (subinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, subinfo->dobj.catId, subinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = subinfo->dobj.name,
								  .owner = subinfo->rolname,
								  .description = "SUBSCRIPTION",
								  .section = SECTION_POST_DATA,
								  .createStmt = query->data,
								  .dropStmt = delq->data));

	if (subinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "SUBSCRIPTION", qsubname,
					NULL, subinfo->rolname,
					subinfo->dobj.catId, 0, subinfo->dobj.dumpId);

	if (subinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "SUBSCRIPTION", qsubname,
					 NULL, subinfo->rolname,
					 subinfo->dobj.catId, 0, subinfo->dobj.dumpId);

	destroyPQExpBuffer(publications);
	free(pubnames);

	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(query);
	free(qsubname);
}

static void
dumpPublication(Archive *fout, const PublicationInfo *pubinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer delq;
	PQExpBuffer query;
	char	   *qpubname;
	bool		first = true;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	delq = createPQExpBuffer();
	query = createPQExpBuffer();

	qpubname = pg_strdup(fmtId(pubinfo->dobj.name));

	appendPQExpBuffer(delq, "DROP PUBLICATION %s;\n",
					  qpubname);

	appendPQExpBuffer(query, "CREATE PUBLICATION %s",
					  qpubname);

	if (pubinfo->puballtables)
		appendPQExpBufferStr(query, " FOR ALL TABLES");

	appendPQExpBufferStr(query, " WITH (publish = '");
	if (pubinfo->pubinsert)
	{
		appendPQExpBufferStr(query, "insert");
		first = false;
	}

	if (pubinfo->pubupdate)
	{
		if (!first)
			appendPQExpBufferStr(query, ", ");

		appendPQExpBufferStr(query, "update");
		first = false;
	}

	if (pubinfo->pubdelete)
	{
		if (!first)
			appendPQExpBufferStr(query, ", ");

		appendPQExpBufferStr(query, "delete");
		first = false;
	}

	if (pubinfo->pubtruncate)
	{
		if (!first)
			appendPQExpBufferStr(query, ", ");

		appendPQExpBufferStr(query, "truncate");
		first = false;
	}

	appendPQExpBufferChar(query, '\'');

	if (pubinfo->pubviaroot)
		appendPQExpBufferStr(query, ", publish_via_partition_root = true");

	appendPQExpBufferStr(query, ");\n");

	if (pubinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, pubinfo->dobj.catId, pubinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = pubinfo->dobj.name,
								  .owner = pubinfo->rolname,
								  .description = "PUBLICATION",
								  .section = SECTION_POST_DATA,
								  .createStmt = query->data,
								  .dropStmt = delq->data));

	if (pubinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "PUBLICATION", qpubname,
					NULL, pubinfo->rolname,
					pubinfo->dobj.catId, 0, pubinfo->dobj.dumpId);

	if (pubinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "PUBLICATION", qpubname,
					 NULL, pubinfo->rolname,
					 pubinfo->dobj.catId, 0, pubinfo->dobj.dumpId);

	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(query);
	free(qpubname);
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

void
AddAcl(PQExpBuffer aclbuf, const char *keyword, const char *subname)
{
	if (aclbuf->len > 0)
		appendPQExpBufferChar(aclbuf, ',');
	appendPQExpBufferStr(aclbuf, keyword);
	if (subname)
		appendPQExpBuffer(aclbuf, "(%s)", subname);
}

static void
appendReloptionsArrayAH(PQExpBuffer buffer, const char *reloptions,
						const char *prefix, Archive *fout)
{
	bool		res;

	res = appendReloptionsArray(buffer, reloptions, prefix, fout->encoding,
								fout->std_strings);
	if (!res)
		pg_log_warning("could not parse %s array", "reloptions");
}

static const char *
getFormattedTypeName(Archive *fout, Oid oid, OidOptions opts)
{
	TypeInfo   *typeInfo;
	char	   *result;
	PQExpBuffer query;
	PGresult   *res;

	if (oid == 0)
	{
		if ((opts & zeroAsStar) != 0)
			return "*";
		else if ((opts & zeroAsNone) != 0)
			return "NONE";
	}

	/* see if we have the result cached in the type's TypeInfo record */
	typeInfo = findTypeByOid(oid);
	if (typeInfo && typeInfo->ftypname)
		return typeInfo->ftypname;

	query = createPQExpBuffer();
	appendPQExpBuffer(query, "SELECT pg_catalog.format_type('%u'::pg_catalog.oid, NULL)",
					  oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	/* result of format_type is already quoted */
	result = pg_strdup(PQgetvalue(res, 0, 0));

	PQclear(res);
	destroyPQExpBuffer(query);

	/*
	 * Cache the result for re-use in later requests, if possible.  If we
	 * don't have a TypeInfo for the type, the string will be leaked once the
	 * caller is done with it ... but that case really should not happen, so
	 * leaking if it does seems acceptable.
	 */
	if (typeInfo)
		typeInfo->ftypname = result;

	return result;
}


static void
dumpEncoding(Archive *AH)
{
	const char *encname = pg_encoding_to_char(AH->encoding);
	PQExpBuffer qry = createPQExpBuffer();

	pg_log_info("saving encoding = %s", encname);

	appendPQExpBufferStr(qry, "SET client_encoding = ");
	appendStringLiteralAH(qry, encname, AH);
	appendPQExpBufferStr(qry, ";\n");

	ArchiveEntry(AH, nilCatalogId, createDumpId(),
				 ARCHIVE_OPTS(.tag = "ENCODING",
							  .description = "ENCODING",
							  .section = SECTION_PRE_DATA,
							  .createStmt = qry->data));

	destroyPQExpBuffer(qry);
}

static int
findComments(Oid classoid, Oid objoid, CommentItem **items)
{
	CommentItem *middle = NULL;
	CommentItem *low;
	CommentItem *high;
	int			nmatch;

	/*
	 * Do binary search to find some item matching the object.
	 */
	low = &comments[0];
	high = &comments[ncomments - 1];
	while (low <= high)
	{
		middle = low + (high - low) / 2;

		if (classoid < middle->classoid)
			high = middle - 1;
		else if (classoid > middle->classoid)
			low = middle + 1;
		else if (objoid < middle->objoid)
			high = middle - 1;
		else if (objoid > middle->objoid)
			low = middle + 1;
		else
			break;				/* found a match */
	}

	if (low > high)				/* no matches */
	{
		*items = NULL;
		return 0;
	}

	/*
	 * Now determine how many items match the object.  The search loop
	 * invariant still holds: only items between low and high inclusive could
	 * match.
	 */
	nmatch = 1;
	while (middle > low)
	{
		if (classoid != middle[-1].classoid ||
			objoid != middle[-1].objoid)
			break;
		middle--;
		nmatch++;
	}

	*items = middle;

	middle += nmatch;
	while (middle <= high)
	{
		if (classoid != middle->classoid ||
			objoid != middle->objoid)
			break;
		middle++;
		nmatch++;
	}

	return nmatch;
}


static void
append_depends_on_extension(Archive *fout,
							PQExpBuffer create,
							const DumpableObject *dobj,
							const char *catalog,
							const char *keyword,
							const char *objname)
{
	if (dobj->depends_on_ext)
	{
		char	   *nm;
		PGresult   *res;
		PQExpBuffer query;
		int			ntups;
		int			i_extname;
		int			i;

		/* dodge fmtId() non-reentrancy */
		nm = pg_strdup(objname);

		query = createPQExpBuffer();
		appendPQExpBuffer(query,
						  "SELECT e.extname "
						  "FROM pg_catalog.pg_depend d, pg_catalog.pg_extension e "
						  "WHERE d.refobjid = e.oid AND classid = '%s'::pg_catalog.regclass "
						  "AND objid = '%u'::pg_catalog.oid AND deptype = 'x' "
						  "AND refclassid = 'pg_catalog.pg_extension'::pg_catalog.regclass",
						  catalog,
						  dobj->catId.oid);
		res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);
		ntups = PQntuples(res);
		i_extname = PQfnumber(res, "extname");
		for (i = 0; i < ntups; i++)
		{
			appendPQExpBuffer(create, "\nALTER %s %s DEPENDS ON EXTENSION %s;",
							  keyword, nm,
							  fmtId(PQgetvalue(res, i, i_extname)));
		}

		PQclear(res);
		destroyPQExpBuffer(query);
		pg_free(nm);
	}
}

/*
 * dumpStdStrings: put the correct escape string behavior into the archive
 */
static void
dumpStdStrings(Archive *AH)
{
	const char *stdstrings = AH->std_strings ? "on" : "off";
	PQExpBuffer qry = createPQExpBuffer();

	pg_log_info("saving \"standard_conforming_strings = %s\"",
				stdstrings);

	appendPQExpBuffer(qry, "SET standard_conforming_strings = '%s';\n",
					  stdstrings);

	ArchiveEntry(AH, nilCatalogId, createDumpId(),
				 ARCHIVE_OPTS(.tag = "STDSTRINGS",
							  .description = "STDSTRINGS",
							  .section = SECTION_PRE_DATA,
							  .createStmt = qry->data));

	destroyPQExpBuffer(qry);
}

/*
 * dumpSearchPath: record the active search_path in the archive
 */
static void
dumpSearchPath(Archive *AH)
{
	PQExpBuffer qry = createPQExpBuffer();
	PQExpBuffer path = createPQExpBuffer();
	PGresult   *res;
	char	  **schemanames = NULL;
	int			nschemanames = 0;
	int			i;

	/*
	 * We use the result of current_schemas(), not the search_path GUC,
	 * because that might contain wildcards such as "$user", which won't
	 * necessarily have the same value during restore.  Also, this way avoids
	 * listing schemas that may appear in search_path but not actually exist,
	 * which seems like a prudent exclusion.
	 */
	res = ExecuteSqlQueryForSingleRow(AH,
									  "SELECT pg_catalog.current_schemas(false)");

	if (!parsePGArray(PQgetvalue(res, 0, 0), &schemanames, &nschemanames))
		pg_fatal("could not parse result of current_schemas()");

	/*
	 * We use set_config(), not a simple "SET search_path" command, because
	 * the latter has less-clean behavior if the search path is empty.  While
	 * that's likely to get fixed at some point, it seems like a good idea to
	 * be as backwards-compatible as possible in what we put into archives.
	 */
	for (i = 0; i < nschemanames; i++)
	{
		if (i > 0)
			appendPQExpBufferStr(path, ", ");
		appendPQExpBufferStr(path, fmtId(schemanames[i]));
	}

	appendPQExpBufferStr(qry, "SELECT pg_catalog.set_config('search_path', ");
	appendStringLiteralAH(qry, path->data, AH);
	appendPQExpBufferStr(qry, ", false);\n");

	pg_log_info("saving \"search_path = %s\"", path->data);

	ArchiveEntry(AH, nilCatalogId, createDumpId(),
				 ARCHIVE_OPTS(.tag = "SEARCHPATH",
							  .description = "SEARCHPATH",
							  .section = SECTION_PRE_DATA,
							  .createStmt = qry->data));

	/* Also save it in AH->searchpath, in case we're doing plain text dump */
	AH->searchpath = pg_strdup(qry->data);

	free(schemanames);
	PQclear(res);
	destroyPQExpBuffer(qry);
	destroyPQExpBuffer(path);
}

static void
dumpNamespace(Archive *fout, const NamespaceInfo *nspinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PQExpBuffer delq;
	char	   *qnspname;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();

	qnspname = pg_strdup(fmtId(nspinfo->dobj.name));

	if (nspinfo->create)
	{
		appendPQExpBuffer(delq, "DROP SCHEMA %s;\n", qnspname);
		appendPQExpBuffer(q, "CREATE SCHEMA %s;\n", qnspname);
	}
	else
	{
		/* see selectDumpableNamespace() */
		appendPQExpBufferStr(delq,
							 "-- *not* dropping schema, since initdb creates it\n");
		appendPQExpBufferStr(q,
							 "-- *not* creating schema, since initdb creates it\n");
	}

	

	if (nspinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, nspinfo->dobj.catId, nspinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = nspinfo->dobj.name,
								  .owner = nspinfo->rolname,
								  .description = "SCHEMA",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Schema Comments and Security Labels */
	if (nspinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
	{
		const char *initdb_comment = NULL;

		if (!nspinfo->create && strcmp(qnspname, "public") == 0)
			initdb_comment = "standard public schema";
		dumpCommentExtended(fout, "SCHEMA", qnspname,
							NULL, nspinfo->rolname,
							nspinfo->dobj.catId, 0, nspinfo->dobj.dumpId,
							initdb_comment);
	}

	if (nspinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "SCHEMA", qnspname,
					 NULL, nspinfo->rolname,
					 nspinfo->dobj.catId, 0, nspinfo->dobj.dumpId);

	if (nspinfo->dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, nspinfo->dobj.dumpId, InvalidDumpId, "SCHEMA",
				qnspname, NULL, NULL,
				NULL, nspinfo->rolname, &nspinfo->dacl);

	free(qnspname);

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
}

/*
 * dumpExtension
 *	  writes out to fout the queries to recreate an extension
 */
static void
dumpExtension(Archive *fout, const ExtensionInfo *extinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PQExpBuffer delq;
	char	   *qextname;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();

	qextname = pg_strdup(fmtId(extinfo->dobj.name));

	appendPQExpBuffer(delq, "DROP EXTENSION %s;\n", qextname);

	if (!dopt->binary_upgrade)
	{
		/*
		 * In a regular dump, we simply create the extension, intentionally
		 * not specifying a version, so that the destination installation's
		 * default version is used.
		 *
		 * Use of IF NOT EXISTS here is unlike our behavior for other object
		 * types; but there are various scenarios in which it's convenient to
		 * manually create the desired extension before restoring, so we
		 * prefer to allow it to exist already.
		 */
		appendPQExpBuffer(q, "CREATE EXTENSION IF NOT EXISTS %s WITH SCHEMA %s;\n",
						  qextname, fmtId(extinfo->namespace));
	}
	else
	{
		/*
		 * In binary-upgrade mode, it's critical to reproduce the state of the
		 * database exactly, so our procedure is to create an empty extension,
		 * restore all the contained objects normally, and add them to the
		 * extension one by one.  This function performs just the first of
		 * those steps.  binary_upgrade_extension_member() takes care of
		 * adding member objects as they're created.
		 */
		int			i;
		int			n;

		appendPQExpBufferStr(q, "-- For binary upgrade, create an empty extension and insert objects into it\n");

		/*
		 * We unconditionally create the extension, so we must drop it if it
		 * exists.  This could happen if the user deleted 'plpgsql' and then
		 * readded it, causing its oid to be greater than g_last_builtin_oid.
		 */
		appendPQExpBuffer(q, "DROP EXTENSION IF EXISTS %s;\n", qextname);

		appendPQExpBufferStr(q,
							 "SELECT pg_catalog.binary_upgrade_create_empty_extension(");
		appendStringLiteralAH(q, extinfo->dobj.name, fout);
		appendPQExpBufferStr(q, ", ");
		appendStringLiteralAH(q, extinfo->namespace, fout);
		appendPQExpBufferStr(q, ", ");
		appendPQExpBuffer(q, "%s, ", extinfo->relocatable ? "true" : "false");
		appendStringLiteralAH(q, extinfo->extversion, fout);
		appendPQExpBufferStr(q, ", ");

		/*
		 * Note that we're pushing extconfig (an OID array) back into
		 * pg_extension exactly as-is.  This is OK because pg_class OIDs are
		 * preserved in binary upgrade.
		 */
		if (strlen(extinfo->extconfig) > 2)
			appendStringLiteralAH(q, extinfo->extconfig, fout);
		else
			appendPQExpBufferStr(q, "NULL");
		appendPQExpBufferStr(q, ", ");
		if (strlen(extinfo->extcondition) > 2)
			appendStringLiteralAH(q, extinfo->extcondition, fout);
		else
			appendPQExpBufferStr(q, "NULL");
		appendPQExpBufferStr(q, ", ");
		appendPQExpBufferStr(q, "ARRAY[");
		n = 0;
		for (i = 0; i < extinfo->dobj.nDeps; i++)
		{
			DumpableObject *extobj;

			extobj = findObjectByDumpId(extinfo->dobj.dependencies[i]);
			if (extobj && extobj->objType == DO_EXTENSION)
			{
				if (n++ > 0)
					appendPQExpBufferChar(q, ',');
				appendStringLiteralAH(q, extobj->name, fout);
			}
		}
		appendPQExpBufferStr(q, "]::pg_catalog.text[]");
		appendPQExpBufferStr(q, ");\n");
	}

	if (extinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, extinfo->dobj.catId, extinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = extinfo->dobj.name,
								  .description = "EXTENSION",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Extension Comments and Security Labels */
	if (extinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "EXTENSION", qextname,
					NULL, "",
					extinfo->dobj.catId, 0, extinfo->dobj.dumpId);

	if (extinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "EXTENSION", qextname,
					 NULL, "",
					 extinfo->dobj.catId, 0, extinfo->dobj.dumpId);

	free(qextname);

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
}

/*
 * dumpType
 *	  writes out to fout the queries to recreate a user-defined type
 */
static void
dumpType(Archive *fout, const TypeInfo *tyinfo)
{
	DumpOptions *dopt = fout->dopt;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	/* Dump out in proper style */
	if (tyinfo->typtype == TYPTYPE_BASE)
		dumpBaseType(fout, tyinfo);
	else if (tyinfo->typtype == TYPTYPE_DOMAIN)
		dumpDomain(fout, tyinfo);
	else if (tyinfo->typtype == TYPTYPE_COMPOSITE)
		dumpCompositeType(fout, tyinfo);
	else if (tyinfo->typtype == TYPTYPE_ENUM)
		dumpEnumType(fout, tyinfo);
	else if (tyinfo->typtype == TYPTYPE_RANGE)
		dumpRangeType(fout, tyinfo);
	else if (tyinfo->typtype == TYPTYPE_PSEUDO && !tyinfo->isDefined)
		dumpUndefinedType(fout, tyinfo);
	else
		pg_log_warning("typtype of data type \"%s\" appears to be invalid",
					   tyinfo->dobj.name);
}

/*
 * dumpEnumType
 *	  writes out to fout the queries to recreate a user-defined enum type
 */
static void
dumpEnumType(Archive *fout, const TypeInfo *tyinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q = createPQExpBuffer();
	PQExpBuffer delq = createPQExpBuffer();
	PQExpBuffer query = createPQExpBuffer();
	PGresult   *res;
	int			num,
				i;
	Oid			enum_oid;
	char	   *qtypname;
	char	   *qualtypname;
	char	   *label;
	int			i_enumlabel;
	int			i_oid;

	if (!fout->is_prepared[PREPQUERY_DUMPENUMTYPE])
	{
		/* Set up query for enum-specific details */
		appendPQExpBufferStr(query,
							 "PREPARE dumpEnumType(pg_catalog.oid) AS\n"
							 "SELECT oid, enumlabel "
							 "FROM pg_catalog.pg_enum "
							 "WHERE enumtypid = $1 "
							 "ORDER BY enumsortorder");

		ExecuteSqlStatement(fout, query->data);

		fout->is_prepared[PREPQUERY_DUMPENUMTYPE] = true;
	}

	printfPQExpBuffer(query,
					  "EXECUTE dumpEnumType('%u')",
					  tyinfo->dobj.catId.oid);

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	num = PQntuples(res);

	qtypname = pg_strdup(fmtId(tyinfo->dobj.name));
	qualtypname = pg_strdup(fmtQualifiedDumpable(tyinfo));

	/*
	 * CASCADE shouldn't be required here as for normal types since the I/O
	 * functions are generic and do not get dropped.
	 */
	appendPQExpBuffer(delq, "DROP TYPE %s;\n", qualtypname);

	

	appendPQExpBuffer(q, "CREATE TYPE %s AS ENUM (",
					  qualtypname);

	if (!dopt->binary_upgrade)
	{
		i_enumlabel = PQfnumber(res, "enumlabel");

		/* Labels with server-assigned oids */
		for (i = 0; i < num; i++)
		{
			label = PQgetvalue(res, i, i_enumlabel);
			if (i > 0)
				appendPQExpBufferChar(q, ',');
			appendPQExpBufferStr(q, "\n    ");
			appendStringLiteralAH(q, label, fout);
		}
	}

	appendPQExpBufferStr(q, "\n);\n");

	if (dopt->binary_upgrade)
	{
		i_oid = PQfnumber(res, "oid");
		i_enumlabel = PQfnumber(res, "enumlabel");

		/* Labels with dump-assigned (preserved) oids */
		for (i = 0; i < num; i++)
		{
			enum_oid = atooid(PQgetvalue(res, i, i_oid));
			label = PQgetvalue(res, i, i_enumlabel);

			if (i == 0)
				appendPQExpBufferStr(q, "\n-- For binary upgrade, must preserve pg_enum oids\n");
			appendPQExpBuffer(q,
							  "SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('%u'::pg_catalog.oid);\n",
							  enum_oid);
			appendPQExpBuffer(q, "ALTER TYPE %s ADD VALUE ", qualtypname);
			appendStringLiteralAH(q, label, fout);
			appendPQExpBufferStr(q, ";\n\n");
		}
	}

	

	if (tyinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, tyinfo->dobj.catId, tyinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tyinfo->dobj.name,
								  .namespace = tyinfo->dobj.namespace->dobj.name,
								  .owner = tyinfo->rolname,
								  .description = "TYPE",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Type Comments and Security Labels */
	if (tyinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "TYPE", qtypname,
					tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "TYPE", qtypname,
					 tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					 tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, tyinfo->dobj.dumpId, InvalidDumpId, "TYPE",
				qtypname, NULL,
				tyinfo->dobj.namespace->dobj.name,
				NULL, tyinfo->rolname, &tyinfo->dacl);

	PQclear(res);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(query);
	free(qtypname);
	free(qualtypname);
}

/*
 * dumpRangeType
 *	  writes out to fout the queries to recreate a user-defined range type
 */
static void
dumpRangeType(Archive *fout, const TypeInfo *tyinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q = createPQExpBuffer();
	PQExpBuffer delq = createPQExpBuffer();
	PQExpBuffer query = createPQExpBuffer();
	PGresult   *res;
	Oid			collationOid;
	char	   *qtypname;
	char	   *qualtypname;
	char	   *procname;

	if (!fout->is_prepared[PREPQUERY_DUMPRANGETYPE])
	{
		/* Set up query for range-specific details */
		appendPQExpBufferStr(query,
							 "PREPARE dumpRangeType(pg_catalog.oid) AS\n");

		appendPQExpBufferStr(query,
							 "SELECT ");

		if (fout->remoteVersion >= 140000)
			appendPQExpBufferStr(query,
								 "pg_catalog.format_type(rngmultitypid, NULL) AS rngmultitype, ");
		else
			appendPQExpBufferStr(query,
								 "NULL AS rngmultitype, ");

		appendPQExpBufferStr(query,
							 "pg_catalog.format_type(rngsubtype, NULL) AS rngsubtype, "
							 "opc.opcname AS opcname, "
							 "(SELECT nspname FROM pg_catalog.pg_namespace nsp "
							 "  WHERE nsp.oid = opc.opcnamespace) AS opcnsp, "
							 "opc.opcdefault, "
							 "CASE WHEN rngcollation = st.typcollation THEN 0 "
							 "     ELSE rngcollation END AS collation, "
							 "rngcanonical, rngsubdiff "
							 "FROM pg_catalog.pg_range r, pg_catalog.pg_type st, "
							 "     pg_catalog.pg_opclass opc "
							 "WHERE st.oid = rngsubtype AND opc.oid = rngsubopc AND "
							 "rngtypid = $1");

		ExecuteSqlStatement(fout, query->data);

		fout->is_prepared[PREPQUERY_DUMPRANGETYPE] = true;
	}

	printfPQExpBuffer(query,
					  "EXECUTE dumpRangeType('%u')",
					  tyinfo->dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	qtypname = pg_strdup(fmtId(tyinfo->dobj.name));
	qualtypname = pg_strdup(fmtQualifiedDumpable(tyinfo));

	/*
	 * CASCADE shouldn't be required here as for normal types since the I/O
	 * functions are generic and do not get dropped.
	 */
	appendPQExpBuffer(delq, "DROP TYPE %s;\n", qualtypname);

	

	appendPQExpBuffer(q, "CREATE TYPE %s AS RANGE (",
					  qualtypname);

	appendPQExpBuffer(q, "\n    subtype = %s",
					  PQgetvalue(res, 0, PQfnumber(res, "rngsubtype")));

	if (!PQgetisnull(res, 0, PQfnumber(res, "rngmultitype")))
		appendPQExpBuffer(q, ",\n    multirange_type_name = %s",
						  PQgetvalue(res, 0, PQfnumber(res, "rngmultitype")));

	/* print subtype_opclass only if not default for subtype */
	if (PQgetvalue(res, 0, PQfnumber(res, "opcdefault"))[0] != 't')
	{
		char	   *opcname = PQgetvalue(res, 0, PQfnumber(res, "opcname"));
		char	   *nspname = PQgetvalue(res, 0, PQfnumber(res, "opcnsp"));

		appendPQExpBuffer(q, ",\n    subtype_opclass = %s.",
						  fmtId(nspname));
		appendPQExpBufferStr(q, fmtId(opcname));
	}

	collationOid = atooid(PQgetvalue(res, 0, PQfnumber(res, "collation")));
	if (OidIsValid(collationOid))
	{
		CollInfo   *coll = findCollationByOid(collationOid);

		if (coll)
			appendPQExpBuffer(q, ",\n    collation = %s",
							  fmtQualifiedDumpable(coll));
	}

	procname = PQgetvalue(res, 0, PQfnumber(res, "rngcanonical"));
	if (strcmp(procname, "-") != 0)
		appendPQExpBuffer(q, ",\n    canonical = %s", procname);

	procname = PQgetvalue(res, 0, PQfnumber(res, "rngsubdiff"));
	if (strcmp(procname, "-") != 0)
		appendPQExpBuffer(q, ",\n    subtype_diff = %s", procname);

	appendPQExpBufferStr(q, "\n);\n");

	

	if (tyinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, tyinfo->dobj.catId, tyinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tyinfo->dobj.name,
								  .namespace = tyinfo->dobj.namespace->dobj.name,
								  .owner = tyinfo->rolname,
								  .description = "TYPE",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Type Comments and Security Labels */
	if (tyinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "TYPE", qtypname,
					tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "TYPE", qtypname,
					 tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					 tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, tyinfo->dobj.dumpId, InvalidDumpId, "TYPE",
				qtypname, NULL,
				tyinfo->dobj.namespace->dobj.name,
				NULL, tyinfo->rolname, &tyinfo->dacl);

	PQclear(res);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(query);
	free(qtypname);
	free(qualtypname);
}

/*
 * dumpUndefinedType
 *	  writes out to fout the queries to recreate a !typisdefined type
 *
 * This is a shell type, but we use different terminology to distinguish
 * this case from where we have to emit a shell type definition to break
 * circular dependencies.  An undefined type shouldn't ever have anything
 * depending on it.
 */
static void
dumpUndefinedType(Archive *fout, const TypeInfo *tyinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q = createPQExpBuffer();
	PQExpBuffer delq = createPQExpBuffer();
	char	   *qtypname;
	char	   *qualtypname;

	qtypname = pg_strdup(fmtId(tyinfo->dobj.name));
	qualtypname = pg_strdup(fmtQualifiedDumpable(tyinfo));

	appendPQExpBuffer(delq, "DROP TYPE %s;\n", qualtypname);

	
	appendPQExpBuffer(q, "CREATE TYPE %s;\n",
					  qualtypname);

	

	if (tyinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, tyinfo->dobj.catId, tyinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tyinfo->dobj.name,
								  .namespace = tyinfo->dobj.namespace->dobj.name,
								  .owner = tyinfo->rolname,
								  .description = "TYPE",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Type Comments and Security Labels */
	if (tyinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "TYPE", qtypname,
					tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "TYPE", qtypname,
					 tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					 tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, tyinfo->dobj.dumpId, InvalidDumpId, "TYPE",
				qtypname, NULL,
				tyinfo->dobj.namespace->dobj.name,
				NULL, tyinfo->rolname, &tyinfo->dacl);

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	free(qtypname);
	free(qualtypname);
}

/*
 * dumpBaseType
 *	  writes out to fout the queries to recreate a user-defined base type
 */
static void
dumpBaseType(Archive *fout, const TypeInfo *tyinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q = createPQExpBuffer();
	PQExpBuffer delq = createPQExpBuffer();
	PQExpBuffer query = createPQExpBuffer();
	PGresult   *res;
	char	   *qtypname;
	char	   *qualtypname;
	char	   *typlen;
	char	   *typinput;
	char	   *typoutput;
	char	   *typreceive;
	char	   *typsend;
	char	   *typmodin;
	char	   *typmodout;
	char	   *typanalyze;
	char	   *typsubscript;
	Oid			typreceiveoid;
	Oid			typsendoid;
	Oid			typmodinoid;
	Oid			typmodoutoid;
	Oid			typanalyzeoid;
	Oid			typsubscriptoid;
	char	   *typcategory;
	char	   *typispreferred;
	char	   *typdelim;
	char	   *typbyval;
	char	   *typalign;
	char	   *typstorage;
	char	   *typcollatable;
	char	   *typdefault;
	bool		typdefault_is_literal = false;

	if (!fout->is_prepared[PREPQUERY_DUMPBASETYPE])
	{
		/* Set up query for type-specific details */
		appendPQExpBufferStr(query,
							 "PREPARE dumpBaseType(pg_catalog.oid) AS\n"
							 "SELECT typlen, "
							 "typinput, typoutput, typreceive, typsend, "
							 "typreceive::pg_catalog.oid AS typreceiveoid, "
							 "typsend::pg_catalog.oid AS typsendoid, "
							 "typanalyze, "
							 "typanalyze::pg_catalog.oid AS typanalyzeoid, "
							 "typdelim, typbyval, typalign, typstorage, "
							 "typmodin, typmodout, "
							 "typmodin::pg_catalog.oid AS typmodinoid, "
							 "typmodout::pg_catalog.oid AS typmodoutoid, "
							 "typcategory, typispreferred, "
							 "(typcollation <> 0) AS typcollatable, "
							 "pg_catalog.pg_get_expr(typdefaultbin, 0) AS typdefaultbin, typdefault, ");

		if (fout->remoteVersion >= 140000)
			appendPQExpBufferStr(query,
								 "typsubscript, "
								 "typsubscript::pg_catalog.oid AS typsubscriptoid ");
		else
			appendPQExpBufferStr(query,
								 "'-' AS typsubscript, 0 AS typsubscriptoid ");

		appendPQExpBufferStr(query, "FROM pg_catalog.pg_type "
							 "WHERE oid = $1");

		ExecuteSqlStatement(fout, query->data);

		fout->is_prepared[PREPQUERY_DUMPBASETYPE] = true;
	}

	printfPQExpBuffer(query,
					  "EXECUTE dumpBaseType('%u')",
					  tyinfo->dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	typlen = PQgetvalue(res, 0, PQfnumber(res, "typlen"));
	typinput = PQgetvalue(res, 0, PQfnumber(res, "typinput"));
	typoutput = PQgetvalue(res, 0, PQfnumber(res, "typoutput"));
	typreceive = PQgetvalue(res, 0, PQfnumber(res, "typreceive"));
	typsend = PQgetvalue(res, 0, PQfnumber(res, "typsend"));
	typmodin = PQgetvalue(res, 0, PQfnumber(res, "typmodin"));
	typmodout = PQgetvalue(res, 0, PQfnumber(res, "typmodout"));
	typanalyze = PQgetvalue(res, 0, PQfnumber(res, "typanalyze"));
	typsubscript = PQgetvalue(res, 0, PQfnumber(res, "typsubscript"));
	typreceiveoid = atooid(PQgetvalue(res, 0, PQfnumber(res, "typreceiveoid")));
	typsendoid = atooid(PQgetvalue(res, 0, PQfnumber(res, "typsendoid")));
	typmodinoid = atooid(PQgetvalue(res, 0, PQfnumber(res, "typmodinoid")));
	typmodoutoid = atooid(PQgetvalue(res, 0, PQfnumber(res, "typmodoutoid")));
	typanalyzeoid = atooid(PQgetvalue(res, 0, PQfnumber(res, "typanalyzeoid")));
	typsubscriptoid = atooid(PQgetvalue(res, 0, PQfnumber(res, "typsubscriptoid")));
	typcategory = PQgetvalue(res, 0, PQfnumber(res, "typcategory"));
	typispreferred = PQgetvalue(res, 0, PQfnumber(res, "typispreferred"));
	typdelim = PQgetvalue(res, 0, PQfnumber(res, "typdelim"));
	typbyval = PQgetvalue(res, 0, PQfnumber(res, "typbyval"));
	typalign = PQgetvalue(res, 0, PQfnumber(res, "typalign"));
	typstorage = PQgetvalue(res, 0, PQfnumber(res, "typstorage"));
	typcollatable = PQgetvalue(res, 0, PQfnumber(res, "typcollatable"));
	if (!PQgetisnull(res, 0, PQfnumber(res, "typdefaultbin")))
		typdefault = PQgetvalue(res, 0, PQfnumber(res, "typdefaultbin"));
	else if (!PQgetisnull(res, 0, PQfnumber(res, "typdefault")))
	{
		typdefault = PQgetvalue(res, 0, PQfnumber(res, "typdefault"));
		typdefault_is_literal = true;	/* it needs quotes */
	}
	else
		typdefault = NULL;

	qtypname = pg_strdup(fmtId(tyinfo->dobj.name));
	qualtypname = pg_strdup(fmtQualifiedDumpable(tyinfo));

	/*
	 * The reason we include CASCADE is that the circular dependency between
	 * the type and its I/O functions makes it impossible to drop the type any
	 * other way.
	 */
	appendPQExpBuffer(delq, "DROP TYPE %s CASCADE;\n", qualtypname);

	/*
	 * We might already have a shell type, but setting pg_type_oid is
	 * harmless, and in any case we'd better set the array type OID.
	 */
	

	appendPQExpBuffer(q,
					  "CREATE TYPE %s (\n"
					  "    INTERNALLENGTH = %s",
					  qualtypname,
					  (strcmp(typlen, "-1") == 0) ? "variable" : typlen);

	/* regproc result is sufficiently quoted already */
	appendPQExpBuffer(q, ",\n    INPUT = %s", typinput);
	appendPQExpBuffer(q, ",\n    OUTPUT = %s", typoutput);
	if (OidIsValid(typreceiveoid))
		appendPQExpBuffer(q, ",\n    RECEIVE = %s", typreceive);
	if (OidIsValid(typsendoid))
		appendPQExpBuffer(q, ",\n    SEND = %s", typsend);
	if (OidIsValid(typmodinoid))
		appendPQExpBuffer(q, ",\n    TYPMOD_IN = %s", typmodin);
	if (OidIsValid(typmodoutoid))
		appendPQExpBuffer(q, ",\n    TYPMOD_OUT = %s", typmodout);
	if (OidIsValid(typanalyzeoid))
		appendPQExpBuffer(q, ",\n    ANALYZE = %s", typanalyze);

	if (strcmp(typcollatable, "t") == 0)
		appendPQExpBufferStr(q, ",\n    COLLATABLE = true");

	if (typdefault != NULL)
	{
		appendPQExpBufferStr(q, ",\n    DEFAULT = ");
		if (typdefault_is_literal)
			appendStringLiteralAH(q, typdefault, fout);
		else
			appendPQExpBufferStr(q, typdefault);
	}

	if (OidIsValid(typsubscriptoid))
		appendPQExpBuffer(q, ",\n    SUBSCRIPT = %s", typsubscript);

	if (OidIsValid(tyinfo->typelem))
		appendPQExpBuffer(q, ",\n    ELEMENT = %s",
						  getFormattedTypeName(fout, tyinfo->typelem,
											   zeroIsError));

	if (strcmp(typcategory, "U") != 0)
	{
		appendPQExpBufferStr(q, ",\n    CATEGORY = ");
		appendStringLiteralAH(q, typcategory, fout);
	}

	if (strcmp(typispreferred, "t") == 0)
		appendPQExpBufferStr(q, ",\n    PREFERRED = true");

	if (typdelim && strcmp(typdelim, ",") != 0)
	{
		appendPQExpBufferStr(q, ",\n    DELIMITER = ");
		appendStringLiteralAH(q, typdelim, fout);
	}

	if (*typalign == TYPALIGN_CHAR)
		appendPQExpBufferStr(q, ",\n    ALIGNMENT = char");
	else if (*typalign == TYPALIGN_SHORT)
		appendPQExpBufferStr(q, ",\n    ALIGNMENT = int2");
	else if (*typalign == TYPALIGN_INT)
		appendPQExpBufferStr(q, ",\n    ALIGNMENT = int4");
	else if (*typalign == TYPALIGN_DOUBLE)
		appendPQExpBufferStr(q, ",\n    ALIGNMENT = double");

	if (*typstorage == TYPSTORAGE_PLAIN)
		appendPQExpBufferStr(q, ",\n    STORAGE = plain");
	else if (*typstorage == TYPSTORAGE_EXTERNAL)
		appendPQExpBufferStr(q, ",\n    STORAGE = external");
	else if (*typstorage == TYPSTORAGE_EXTENDED)
		appendPQExpBufferStr(q, ",\n    STORAGE = extended");
	else if (*typstorage == TYPSTORAGE_MAIN)
		appendPQExpBufferStr(q, ",\n    STORAGE = main");

	if (strcmp(typbyval, "t") == 0)
		appendPQExpBufferStr(q, ",\n    PASSEDBYVALUE");

	appendPQExpBufferStr(q, "\n);\n");

	

	if (tyinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, tyinfo->dobj.catId, tyinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tyinfo->dobj.name,
								  .namespace = tyinfo->dobj.namespace->dobj.name,
								  .owner = tyinfo->rolname,
								  .description = "TYPE",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Type Comments and Security Labels */
	if (tyinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "TYPE", qtypname,
					tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "TYPE", qtypname,
					 tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					 tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, tyinfo->dobj.dumpId, InvalidDumpId, "TYPE",
				qtypname, NULL,
				tyinfo->dobj.namespace->dobj.name,
				NULL, tyinfo->rolname, &tyinfo->dacl);

	PQclear(res);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(query);
	free(qtypname);
	free(qualtypname);
}

/*
 * dumpDomain
 *	  writes out to fout the queries to recreate a user-defined domain
 */
static void
dumpDomain(Archive *fout, const TypeInfo *tyinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q = createPQExpBuffer();
	PQExpBuffer delq = createPQExpBuffer();
	PQExpBuffer query = createPQExpBuffer();
	PGresult   *res;
	int			i;
	char	   *qtypname;
	char	   *qualtypname;
	char	   *typnotnull;
	char	   *typdefn;
	char	   *typdefault;
	Oid			typcollation;
	bool		typdefault_is_literal = false;

	if (!fout->is_prepared[PREPQUERY_DUMPDOMAIN])
	{
		/* Set up query for domain-specific details */
		appendPQExpBufferStr(query,
							 "PREPARE dumpDomain(pg_catalog.oid) AS\n");

		appendPQExpBufferStr(query, "SELECT t.typnotnull, "
							 "pg_catalog.format_type(t.typbasetype, t.typtypmod) AS typdefn, "
							 "pg_catalog.pg_get_expr(t.typdefaultbin, 'pg_catalog.pg_type'::pg_catalog.regclass) AS typdefaultbin, "
							 "t.typdefault, "
							 "CASE WHEN t.typcollation <> u.typcollation "
							 "THEN t.typcollation ELSE 0 END AS typcollation "
							 "FROM pg_catalog.pg_type t "
							 "LEFT JOIN pg_catalog.pg_type u ON (t.typbasetype = u.oid) "
							 "WHERE t.oid = $1");

		ExecuteSqlStatement(fout, query->data);

		fout->is_prepared[PREPQUERY_DUMPDOMAIN] = true;
	}

	printfPQExpBuffer(query,
					  "EXECUTE dumpDomain('%u')",
					  tyinfo->dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	typnotnull = PQgetvalue(res, 0, PQfnumber(res, "typnotnull"));
	typdefn = PQgetvalue(res, 0, PQfnumber(res, "typdefn"));
	if (!PQgetisnull(res, 0, PQfnumber(res, "typdefaultbin")))
		typdefault = PQgetvalue(res, 0, PQfnumber(res, "typdefaultbin"));
	else if (!PQgetisnull(res, 0, PQfnumber(res, "typdefault")))
	{
		typdefault = PQgetvalue(res, 0, PQfnumber(res, "typdefault"));
		typdefault_is_literal = true;	/* it needs quotes */
	}
	else
		typdefault = NULL;
	typcollation = atooid(PQgetvalue(res, 0, PQfnumber(res, "typcollation")));

	

	qtypname = pg_strdup(fmtId(tyinfo->dobj.name));
	qualtypname = pg_strdup(fmtQualifiedDumpable(tyinfo));

	appendPQExpBuffer(q,
					  "CREATE DOMAIN %s AS %s",
					  qualtypname,
					  typdefn);

	/* Print collation only if different from base type's collation */
	if (OidIsValid(typcollation))
	{
		CollInfo   *coll;

		coll = findCollationByOid(typcollation);
		if (coll)
			appendPQExpBuffer(q, " COLLATE %s", fmtQualifiedDumpable(coll));
	}

	if (typnotnull[0] == 't')
		appendPQExpBufferStr(q, " NOT NULL");

	if (typdefault != NULL)
	{
		appendPQExpBufferStr(q, " DEFAULT ");
		if (typdefault_is_literal)
			appendStringLiteralAH(q, typdefault, fout);
		else
			appendPQExpBufferStr(q, typdefault);
	}

	PQclear(res);

	/*
	 * Add any CHECK constraints for the domain
	 */
	for (i = 0; i < tyinfo->nDomChecks; i++)
	{
		ConstraintInfo *domcheck = &(tyinfo->domChecks[i]);

		if (!domcheck->separate)
			appendPQExpBuffer(q, "\n\tCONSTRAINT %s %s",
							  fmtId(domcheck->dobj.name), domcheck->condef);
	}

	appendPQExpBufferStr(q, ";\n");

	appendPQExpBuffer(delq, "DROP DOMAIN %s;\n", qualtypname);

	
	if (tyinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, tyinfo->dobj.catId, tyinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tyinfo->dobj.name,
								  .namespace = tyinfo->dobj.namespace->dobj.name,
								  .owner = tyinfo->rolname,
								  .description = "DOMAIN",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Domain Comments and Security Labels */
	if (tyinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "DOMAIN", qtypname,
					tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "DOMAIN", qtypname,
					 tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					 tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, tyinfo->dobj.dumpId, InvalidDumpId, "TYPE",
				qtypname, NULL,
				tyinfo->dobj.namespace->dobj.name,
				NULL, tyinfo->rolname, &tyinfo->dacl);

	/* Dump any per-constraint comments */
	for (i = 0; i < tyinfo->nDomChecks; i++)
	{
		ConstraintInfo *domcheck = &(tyinfo->domChecks[i]);
		PQExpBuffer conprefix = createPQExpBuffer();

		appendPQExpBuffer(conprefix, "CONSTRAINT %s ON DOMAIN",
						  fmtId(domcheck->dobj.name));

		if (domcheck->dobj.dump & DUMP_COMPONENT_COMMENT)
			dumpComment(fout, conprefix->data, qtypname,
						tyinfo->dobj.namespace->dobj.name,
						tyinfo->rolname,
						domcheck->dobj.catId, 0, tyinfo->dobj.dumpId);

		destroyPQExpBuffer(conprefix);
	}

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(query);
	free(qtypname);
	free(qualtypname);
}

/*
 * dumpCompositeType
 *	  writes out to fout the queries to recreate a user-defined stand-alone
 *	  composite type
 */
static void
dumpCompositeType(Archive *fout, const TypeInfo *tyinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q = createPQExpBuffer();
	PQExpBuffer dropped = createPQExpBuffer();
	PQExpBuffer delq = createPQExpBuffer();
	PQExpBuffer query = createPQExpBuffer();
	PGresult   *res;
	char	   *qtypname;
	char	   *qualtypname;
	int			ntups;
	int			i_attname;
	int			i_atttypdefn;
	int			i_attlen;
	int			i_attalign;
	int			i_attisdropped;
	int			i_attcollation;
	int			i;
	int			actual_atts;

	if (!fout->is_prepared[PREPQUERY_DUMPCOMPOSITETYPE])
	{
		/*
		 * Set up query for type-specific details.
		 *
		 * Since we only want to dump COLLATE clauses for attributes whose
		 * collation is different from their type's default, we use a CASE
		 * here to suppress uninteresting attcollations cheaply.  atttypid
		 * will be 0 for dropped columns; collation does not matter for those.
		 */
		appendPQExpBufferStr(query,
							 "PREPARE dumpCompositeType(pg_catalog.oid) AS\n"
							 "SELECT a.attname, a.attnum, "
							 "pg_catalog.format_type(a.atttypid, a.atttypmod) AS atttypdefn, "
							 "a.attlen, a.attalign, a.attisdropped, "
							 "CASE WHEN a.attcollation <> at.typcollation "
							 "THEN a.attcollation ELSE 0 END AS attcollation "
							 "FROM pg_catalog.pg_type ct "
							 "JOIN pg_catalog.pg_attribute a ON a.attrelid = ct.typrelid "
							 "LEFT JOIN pg_catalog.pg_type at ON at.oid = a.atttypid "
							 "WHERE ct.oid = $1 "
							 "ORDER BY a.attnum");

		ExecuteSqlStatement(fout, query->data);

		fout->is_prepared[PREPQUERY_DUMPCOMPOSITETYPE] = true;
	}

	printfPQExpBuffer(query,
					  "EXECUTE dumpCompositeType('%u')",
					  tyinfo->dobj.catId.oid);

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_attname = PQfnumber(res, "attname");
	i_atttypdefn = PQfnumber(res, "atttypdefn");
	i_attlen = PQfnumber(res, "attlen");
	i_attalign = PQfnumber(res, "attalign");
	i_attisdropped = PQfnumber(res, "attisdropped");
	i_attcollation = PQfnumber(res, "attcollation");

	

	qtypname = pg_strdup(fmtId(tyinfo->dobj.name));
	qualtypname = pg_strdup(fmtQualifiedDumpable(tyinfo));

	appendPQExpBuffer(q, "CREATE TYPE %s AS (",
					  qualtypname);

	actual_atts = 0;
	for (i = 0; i < ntups; i++)
	{
		char	   *attname;
		char	   *atttypdefn;
		char	   *attlen;
		char	   *attalign;
		bool		attisdropped;
		Oid			attcollation;

		attname = PQgetvalue(res, i, i_attname);
		atttypdefn = PQgetvalue(res, i, i_atttypdefn);
		attlen = PQgetvalue(res, i, i_attlen);
		attalign = PQgetvalue(res, i, i_attalign);
		attisdropped = (PQgetvalue(res, i, i_attisdropped)[0] == 't');
		attcollation = atooid(PQgetvalue(res, i, i_attcollation));

		if (attisdropped && !dopt->binary_upgrade)
			continue;

		/* Format properly if not first attr */
		if (actual_atts++ > 0)
			appendPQExpBufferChar(q, ',');
		appendPQExpBufferStr(q, "\n\t");

		if (!attisdropped)
		{
			appendPQExpBuffer(q, "%s %s", fmtId(attname), atttypdefn);

			/* Add collation if not default for the column type */
			if (OidIsValid(attcollation))
			{
				CollInfo   *coll;

				coll = findCollationByOid(attcollation);
				if (coll)
					appendPQExpBuffer(q, " COLLATE %s",
									  fmtQualifiedDumpable(coll));
			}
		}
		else
		{
			/*
			 * This is a dropped attribute and we're in binary_upgrade mode.
			 * Insert a placeholder for it in the CREATE TYPE command, and set
			 * length and alignment with direct UPDATE to the catalogs
			 * afterwards. See similar code in dumpTableSchema().
			 */
			appendPQExpBuffer(q, "%s INTEGER /* dummy */", fmtId(attname));

			/* stash separately for insertion after the CREATE TYPE */
			appendPQExpBufferStr(dropped,
								 "\n-- For binary upgrade, recreate dropped column.\n");
			appendPQExpBuffer(dropped, "UPDATE pg_catalog.pg_attribute\n"
							  "SET attlen = %s, "
							  "attalign = '%s', attbyval = false\n"
							  "WHERE attname = ", attlen, attalign);
			appendStringLiteralAH(dropped, attname, fout);
			appendPQExpBufferStr(dropped, "\n  AND attrelid = ");
			appendStringLiteralAH(dropped, qualtypname, fout);
			appendPQExpBufferStr(dropped, "::pg_catalog.regclass;\n");

			appendPQExpBuffer(dropped, "ALTER TYPE %s ",
							  qualtypname);
			appendPQExpBuffer(dropped, "DROP ATTRIBUTE %s;\n",
							  fmtId(attname));
		}
	}
	appendPQExpBufferStr(q, "\n);\n");
	appendPQExpBufferStr(q, dropped->data);

	appendPQExpBuffer(delq, "DROP TYPE %s;\n", qualtypname);

	

	if (tyinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, tyinfo->dobj.catId, tyinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tyinfo->dobj.name,
								  .namespace = tyinfo->dobj.namespace->dobj.name,
								  .owner = tyinfo->rolname,
								  .description = "TYPE",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));


	/* Dump Type Comments and Security Labels */
	if (tyinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "TYPE", qtypname,
					tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "TYPE", qtypname,
					 tyinfo->dobj.namespace->dobj.name, tyinfo->rolname,
					 tyinfo->dobj.catId, 0, tyinfo->dobj.dumpId);

	if (tyinfo->dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, tyinfo->dobj.dumpId, InvalidDumpId, "TYPE",
				qtypname, NULL,
				tyinfo->dobj.namespace->dobj.name,
				NULL, tyinfo->rolname, &tyinfo->dacl);

	/* Dump any per-column comments */
	if (tyinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpCompositeTypeColComments(fout, tyinfo, res);

	PQclear(res);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(dropped);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(query);
	free(qtypname);
	free(qualtypname);
}

/*
 * dumpCompositeTypeColComments
 *	  writes out to fout the queries to recreate comments on the columns of
 *	  a user-defined stand-alone composite type.
 *
 * The caller has already made a query to collect the names and attnums
 * of the type's columns, so we just pass that result into here rather
 * than reading them again.
 */
static void
dumpCompositeTypeColComments(Archive *fout, const TypeInfo *tyinfo,
							 PGresult *res)
{
	CommentItem *comments;
	int			ncomments;
	PQExpBuffer query;
	PQExpBuffer target;
	int			i;
	int			ntups;
	int			i_attname;
	int			i_attnum;
	int			i_attisdropped;

	/* do nothing, if --no-comments is supplied */
	if (fout->dopt->no_comments)
		return;

	/* Search for comments associated with type's pg_class OID */
	ncomments = findComments(RelationRelationId, tyinfo->typrelid,
							 &comments);

	/* If no comments exist, we're done */
	if (ncomments <= 0)
		return;

	/* Build COMMENT ON statements */
	query = createPQExpBuffer();
	target = createPQExpBuffer();

	ntups = PQntuples(res);
	i_attnum = PQfnumber(res, "attnum");
	i_attname = PQfnumber(res, "attname");
	i_attisdropped = PQfnumber(res, "attisdropped");
	while (ncomments > 0)
	{
		const char *attname;

		attname = NULL;
		for (i = 0; i < ntups; i++)
		{
			if (atoi(PQgetvalue(res, i, i_attnum)) == comments->objsubid &&
				PQgetvalue(res, i, i_attisdropped)[0] != 't')
			{
				attname = PQgetvalue(res, i, i_attname);
				break;
			}
		}
		if (attname)			/* just in case we don't find it */
		{
			const char *descr = comments->descr;

			resetPQExpBuffer(target);
			appendPQExpBuffer(target, "COLUMN %s.",
							  fmtId(tyinfo->dobj.name));
			appendPQExpBufferStr(target, fmtId(attname));

			resetPQExpBuffer(query);
			appendPQExpBuffer(query, "COMMENT ON COLUMN %s.",
							  fmtQualifiedDumpable(tyinfo));
			appendPQExpBuffer(query, "%s IS ", fmtId(attname));
			appendStringLiteralAH(query, descr, fout);
			appendPQExpBufferStr(query, ";\n");

			ArchiveEntry(fout, nilCatalogId, createDumpId(),
						 ARCHIVE_OPTS(.tag = target->data,
									  .namespace = tyinfo->dobj.namespace->dobj.name,
									  .owner = tyinfo->rolname,
									  .description = "COMMENT",
									  .section = SECTION_NONE,
									  .createStmt = query->data,
									  .deps = &(tyinfo->dobj.dumpId),
									  .nDeps = 1));
		}

		comments++;
		ncomments--;
	}

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(target);
}

/*
 * dumpShellType
 *	  writes out to fout the queries to create a shell type
 *
 * We dump a shell definition in advance of the I/O functions for the type.
 */
static void
dumpShellType(Archive *fout, const ShellTypeInfo *stinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();

	/*
	 * Note the lack of a DROP command for the shell type; any required DROP
	 * is driven off the base type entry, instead.  This interacts with
	 * _printTocEntry()'s use of the presence of a DROP command to decide
	 * whether an entry needs an ALTER OWNER command.  We don't want to alter
	 * the shell type's owner immediately on creation; that should happen only
	 * after it's filled in, otherwise the backend complains.
	 */

	

	appendPQExpBuffer(q, "CREATE TYPE %s;\n",
					  fmtQualifiedDumpable(stinfo));

	if (stinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, stinfo->dobj.catId, stinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = stinfo->dobj.name,
								  .namespace = stinfo->dobj.namespace->dobj.name,
								  .owner = stinfo->baseType->rolname,
								  .description = "SHELL TYPE",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data));

	destroyPQExpBuffer(q);
}

/*
 * dumpProcLang
 *		  writes out to fout the queries to recreate a user-defined
 *		  procedural language
 */
static void
dumpProcLang(Archive *fout, const ProcLangInfo *plang)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer defqry;
	PQExpBuffer delqry;
	bool		useParams;
	char	   *qlanname;
	FuncInfo   *funcInfo;
	FuncInfo   *inlineInfo = NULL;
	FuncInfo   *validatorInfo = NULL;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	/*
	 * Try to find the support function(s).  It is not an error if we don't
	 * find them --- if the functions are in the pg_catalog schema, as is
	 * standard in 8.1 and up, then we won't have loaded them. (In this case
	 * we will emit a parameterless CREATE LANGUAGE command, which will
	 * require PL template knowledge in the backend to reload.)
	 */

	funcInfo = findFuncByOid(plang->lanplcallfoid);
	if (funcInfo != NULL && !funcInfo->dobj.dump)
		funcInfo = NULL;		/* treat not-dumped same as not-found */

	if (OidIsValid(plang->laninline))
	{
		inlineInfo = findFuncByOid(plang->laninline);
		if (inlineInfo != NULL && !inlineInfo->dobj.dump)
			inlineInfo = NULL;
	}

	if (OidIsValid(plang->lanvalidator))
	{
		validatorInfo = findFuncByOid(plang->lanvalidator);
		if (validatorInfo != NULL && !validatorInfo->dobj.dump)
			validatorInfo = NULL;
	}

	/*
	 * If the functions are dumpable then emit a complete CREATE LANGUAGE with
	 * parameters.  Otherwise, we'll write a parameterless command, which will
	 * be interpreted as CREATE EXTENSION.
	 */
	useParams = (funcInfo != NULL &&
				 (inlineInfo != NULL || !OidIsValid(plang->laninline)) &&
				 (validatorInfo != NULL || !OidIsValid(plang->lanvalidator)));

	defqry = createPQExpBuffer();
	delqry = createPQExpBuffer();

	qlanname = pg_strdup(fmtId(plang->dobj.name));

	appendPQExpBuffer(delqry, "DROP PROCEDURAL LANGUAGE %s;\n",
					  qlanname);

	if (useParams)
	{
		appendPQExpBuffer(defqry, "CREATE %sPROCEDURAL LANGUAGE %s",
						  plang->lanpltrusted ? "TRUSTED " : "",
						  qlanname);
		appendPQExpBuffer(defqry, " HANDLER %s",
						  fmtQualifiedDumpable(funcInfo));
		if (OidIsValid(plang->laninline))
			appendPQExpBuffer(defqry, " INLINE %s",
							  fmtQualifiedDumpable(inlineInfo));
		if (OidIsValid(plang->lanvalidator))
			appendPQExpBuffer(defqry, " VALIDATOR %s",
							  fmtQualifiedDumpable(validatorInfo));
	}
	else
	{
		/*
		 * If not dumping parameters, then use CREATE OR REPLACE so that the
		 * command will not fail if the language is preinstalled in the target
		 * database.
		 *
		 * Modern servers will interpret this as CREATE EXTENSION IF NOT
		 * EXISTS; perhaps we should emit that instead?  But it might just add
		 * confusion.
		 */
		appendPQExpBuffer(defqry, "CREATE OR REPLACE PROCEDURAL LANGUAGE %s",
						  qlanname);
	}
	appendPQExpBufferStr(defqry, ";\n");

	
	if (plang->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, plang->dobj.catId, plang->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = plang->dobj.name,
								  .owner = plang->lanowner,
								  .description = "PROCEDURAL LANGUAGE",
								  .section = SECTION_PRE_DATA,
								  .createStmt = defqry->data,
								  .dropStmt = delqry->data,
								  ));

	/* Dump Proc Lang Comments and Security Labels */
	if (plang->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "LANGUAGE", qlanname,
					NULL, plang->lanowner,
					plang->dobj.catId, 0, plang->dobj.dumpId);

	if (plang->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "LANGUAGE", qlanname,
					 NULL, plang->lanowner,
					 plang->dobj.catId, 0, plang->dobj.dumpId);

	if (plang->lanpltrusted && plang->dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, plang->dobj.dumpId, InvalidDumpId, "LANGUAGE",
				qlanname, NULL, NULL,
				NULL, plang->lanowner, &plang->dacl);

	free(qlanname);

	destroyPQExpBuffer(defqry);
	destroyPQExpBuffer(delqry);
}

/*
 * format_function_arguments: generate function name and argument list
 *
 * This is used when we can rely on pg_get_function_arguments to format
 * the argument list.  Note, however, that pg_get_function_arguments
 * does not special-case zero-argument aggregates.
 */
static char *
format_function_arguments(const FuncInfo *finfo, const char *funcargs, bool is_agg)
{
	PQExpBufferData fn;

	initPQExpBuffer(&fn);
	appendPQExpBufferStr(&fn, fmtId(finfo->dobj.name));
	if (is_agg && finfo->nargs == 0)
		appendPQExpBufferStr(&fn, "(*)");
	else
		appendPQExpBuffer(&fn, "(%s)", funcargs);
	return fn.data;
}

/*
 * format_function_signature: generate function name and argument list
 *
 * Only a minimal list of input argument types is generated; this is
 * sufficient to reference the function, but not to define it.
 *
 * If honor_quotes is false then the function name is never quoted.
 * This is appropriate for use in TOC tags, but not in SQL commands.
 */
static char *
format_function_signature(Archive *fout, const FuncInfo *finfo, bool honor_quotes)
{
	PQExpBufferData fn;
	int			j;

	initPQExpBuffer(&fn);
	if (honor_quotes)
		appendPQExpBuffer(&fn, "%s(", fmtId(finfo->dobj.name));
	else
		appendPQExpBuffer(&fn, "%s(", finfo->dobj.name);
	for (j = 0; j < finfo->nargs; j++)
	{
		if (j > 0)
			appendPQExpBufferStr(&fn, ", ");

		appendPQExpBufferStr(&fn,
							 getFormattedTypeName(fout, finfo->argtypes[j],
												  zeroIsError));
	}
	appendPQExpBufferChar(&fn, ')');
	return fn.data;
}


/*
 * dumpFunc:
 *	  dump out one function
 */
static void
dumpFunc(Archive *fout, const FuncInfo *finfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer query;
	PQExpBuffer q;
	PQExpBuffer delqry;
	PQExpBuffer asPart;
	PGresult   *res;
	char	   *funcsig;		/* identity signature */
	char	   *funcfullsig = NULL; /* full signature */
	char	   *funcsig_tag;
	char	   *qual_funcsig;
	char	   *proretset;
	char	   *prosrc;
	char	   *probin;
	char	   *prosqlbody;
	char	   *funcargs;
	char	   *funciargs;
	char	   *funcresult;
	char	   *protrftypes;
	char	   *prokind;
	char	   *provolatile;
	char	   *proisstrict;
	char	   *prosecdef;
	char	   *proleakproof;
	char	   *proconfig;
	char	   *procost;
	char	   *prorows;
	char	   *prosupport;
	char	   *proparallel;
	char	   *lanname;
	char	  **configitems = NULL;
	int			nconfigitems = 0;
	const char *keyword;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	query = createPQExpBuffer();
	q = createPQExpBuffer();
	delqry = createPQExpBuffer();
	asPart = createPQExpBuffer();

	if (!fout->is_prepared[PREPQUERY_DUMPFUNC])
	{
		/* Set up query for function-specific details */
		appendPQExpBufferStr(query,
							 "PREPARE dumpFunc(pg_catalog.oid) AS\n");

		appendPQExpBufferStr(query,
							 "SELECT\n"
							 "proretset,\n"
							 "prosrc,\n"
							 "probin,\n"
							 "provolatile,\n"
							 "proisstrict,\n"
							 "prosecdef,\n"
							 "lanname,\n"
							 "proconfig,\n"
							 "procost,\n"
							 "prorows,\n"
							 "pg_catalog.pg_get_function_arguments(p.oid) AS funcargs,\n"
							 "pg_catalog.pg_get_function_identity_arguments(p.oid) AS funciargs,\n"
							 "pg_catalog.pg_get_function_result(p.oid) AS funcresult,\n"
							 "proleakproof,\n");

		if (fout->remoteVersion >= 90500)
			appendPQExpBufferStr(query,
								 "array_to_string(protrftypes, ' ') AS protrftypes,\n");
		else
			appendPQExpBufferStr(query,
								 "NULL AS protrftypes,\n");

		if (fout->remoteVersion >= 90600)
			appendPQExpBufferStr(query,
								 "proparallel,\n");
		else
			appendPQExpBufferStr(query,
								 "'u' AS proparallel,\n");

		if (fout->remoteVersion >= 110000)
			appendPQExpBufferStr(query,
								 "prokind,\n");
		else
			appendPQExpBufferStr(query,
								 "CASE WHEN proiswindow THEN 'w' ELSE 'f' END AS prokind,\n");

		if (fout->remoteVersion >= 120000)
			appendPQExpBufferStr(query,
								 "prosupport,\n");
		else
			appendPQExpBufferStr(query,
								 "'-' AS prosupport,\n");

		if (fout->remoteVersion >= 140000)
			appendPQExpBufferStr(query,
								 "pg_get_function_sqlbody(p.oid) AS prosqlbody\n");
		else
			appendPQExpBufferStr(query,
								 "NULL AS prosqlbody\n");

		appendPQExpBufferStr(query,
							 "FROM pg_catalog.pg_proc p, pg_catalog.pg_language l\n"
							 "WHERE p.oid = $1 "
							 "AND l.oid = p.prolang");

		ExecuteSqlStatement(fout, query->data);

		fout->is_prepared[PREPQUERY_DUMPFUNC] = true;
	}

	printfPQExpBuffer(query,
					  "EXECUTE dumpFunc('%u')",
					  finfo->dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	proretset = PQgetvalue(res, 0, PQfnumber(res, "proretset"));
	if (PQgetisnull(res, 0, PQfnumber(res, "prosqlbody")))
	{
		prosrc = PQgetvalue(res, 0, PQfnumber(res, "prosrc"));
		probin = PQgetvalue(res, 0, PQfnumber(res, "probin"));
		prosqlbody = NULL;
	}
	else
	{
		prosrc = NULL;
		probin = NULL;
		prosqlbody = PQgetvalue(res, 0, PQfnumber(res, "prosqlbody"));
	}
	funcargs = PQgetvalue(res, 0, PQfnumber(res, "funcargs"));
	funciargs = PQgetvalue(res, 0, PQfnumber(res, "funciargs"));
	funcresult = PQgetvalue(res, 0, PQfnumber(res, "funcresult"));
	protrftypes = PQgetvalue(res, 0, PQfnumber(res, "protrftypes"));
	prokind = PQgetvalue(res, 0, PQfnumber(res, "prokind"));
	provolatile = PQgetvalue(res, 0, PQfnumber(res, "provolatile"));
	proisstrict = PQgetvalue(res, 0, PQfnumber(res, "proisstrict"));
	prosecdef = PQgetvalue(res, 0, PQfnumber(res, "prosecdef"));
	proleakproof = PQgetvalue(res, 0, PQfnumber(res, "proleakproof"));
	proconfig = PQgetvalue(res, 0, PQfnumber(res, "proconfig"));
	procost = PQgetvalue(res, 0, PQfnumber(res, "procost"));
	prorows = PQgetvalue(res, 0, PQfnumber(res, "prorows"));
	prosupport = PQgetvalue(res, 0, PQfnumber(res, "prosupport"));
	proparallel = PQgetvalue(res, 0, PQfnumber(res, "proparallel"));
	lanname = PQgetvalue(res, 0, PQfnumber(res, "lanname"));

	/*
	 * See backend/commands/functioncmds.c for details of how the 'AS' clause
	 * is used.
	 */
	if (prosqlbody)
	{
		appendPQExpBufferStr(asPart, prosqlbody);
	}
	else if (probin[0] != '\0')
	{
		appendPQExpBufferStr(asPart, "AS ");
		appendStringLiteralAH(asPart, probin, fout);
		if (prosrc[0] != '\0')
		{
			appendPQExpBufferStr(asPart, ", ");

			/*
			 * where we have bin, use dollar quoting if allowed and src
			 * contains quote or backslash; else use regular quoting.
			 */
			if (dopt->disable_dollar_quoting ||
				(strchr(prosrc, '\'') == NULL && strchr(prosrc, '\\') == NULL))
				appendStringLiteralAH(asPart, prosrc, fout);
			else
				appendStringLiteralDQ(asPart, prosrc, NULL);
		}
	}
	else
	{
		appendPQExpBufferStr(asPart, "AS ");
		/* with no bin, dollar quote src unconditionally if allowed */
		if (dopt->disable_dollar_quoting)
			appendStringLiteralAH(asPart, prosrc, fout);
		else
			appendStringLiteralDQ(asPart, prosrc, NULL);
	}

	if (*proconfig)
	{
		if (!parsePGArray(proconfig, &configitems, &nconfigitems))
			pg_fatal("could not parse %s array", "proconfig");
	}
	else
	{
		configitems = NULL;
		nconfigitems = 0;
	}

	funcfullsig = format_function_arguments(finfo, funcargs, false);
	funcsig = format_function_arguments(finfo, funciargs, false);

	funcsig_tag = format_function_signature(fout, finfo, false);

	qual_funcsig = psprintf("%s.%s",
							fmtId(finfo->dobj.namespace->dobj.name),
							funcsig);

	if (prokind[0] == PROKIND_PROCEDURE)
		keyword = "PROCEDURE";
	else
		keyword = "FUNCTION";	/* works for window functions too */

	appendPQExpBuffer(delqry, "DROP %s %s;\n",
					  keyword, qual_funcsig);

	appendPQExpBuffer(q, "CREATE %s %s.%s",
					  keyword,
					  fmtId(finfo->dobj.namespace->dobj.name),
					  funcfullsig ? funcfullsig :
					  funcsig);

	if (prokind[0] == PROKIND_PROCEDURE)
		 /* no result type to output */ ;
	else if (funcresult)
		appendPQExpBuffer(q, " RETURNS %s", funcresult);
	else
		appendPQExpBuffer(q, " RETURNS %s%s",
						  (proretset[0] == 't') ? "SETOF " : "",
						  getFormattedTypeName(fout, finfo->prorettype,
											   zeroIsError));

	appendPQExpBuffer(q, "\n    LANGUAGE %s", fmtId(lanname));

	if (*protrftypes)
	{
		Oid		   *typeids = palloc(FUNC_MAX_ARGS * sizeof(Oid));
		int			i;

		appendPQExpBufferStr(q, " TRANSFORM ");
		parseOidArray(protrftypes, typeids, FUNC_MAX_ARGS);
		for (i = 0; typeids[i]; i++)
		{
			if (i != 0)
				appendPQExpBufferStr(q, ", ");
			appendPQExpBuffer(q, "FOR TYPE %s",
							  getFormattedTypeName(fout, typeids[i], zeroAsNone));
		}
	}

	if (prokind[0] == PROKIND_WINDOW)
		appendPQExpBufferStr(q, " WINDOW");

	if (provolatile[0] != PROVOLATILE_VOLATILE)
	{
		if (provolatile[0] == PROVOLATILE_IMMUTABLE)
			appendPQExpBufferStr(q, " IMMUTABLE");
		else if (provolatile[0] == PROVOLATILE_STABLE)
			appendPQExpBufferStr(q, " STABLE");
		else if (provolatile[0] != PROVOLATILE_VOLATILE)
			pg_fatal("unrecognized provolatile value for function \"%s\"",
					 finfo->dobj.name);
	}

	if (proisstrict[0] == 't')
		appendPQExpBufferStr(q, " STRICT");

	if (prosecdef[0] == 't')
		appendPQExpBufferStr(q, " SECURITY DEFINER");

	if (proleakproof[0] == 't')
		appendPQExpBufferStr(q, " LEAKPROOF");

	/*
	 * COST and ROWS are emitted only if present and not default, so as not to
	 * break backwards-compatibility of the dump without need.  Keep this code
	 * in sync with the defaults in functioncmds.c.
	 */
	if (strcmp(procost, "0") != 0)
	{
		if (strcmp(lanname, "internal") == 0 || strcmp(lanname, "c") == 0)
		{
			/* default cost is 1 */
			if (strcmp(procost, "1") != 0)
				appendPQExpBuffer(q, " COST %s", procost);
		}
		else
		{
			/* default cost is 100 */
			if (strcmp(procost, "100") != 0)
				appendPQExpBuffer(q, " COST %s", procost);
		}
	}
	if (proretset[0] == 't' &&
		strcmp(prorows, "0") != 0 && strcmp(prorows, "1000") != 0)
		appendPQExpBuffer(q, " ROWS %s", prorows);

	if (strcmp(prosupport, "-") != 0)
	{
		/* We rely on regprocout to provide quoting and qualification */
		appendPQExpBuffer(q, " SUPPORT %s", prosupport);
	}

	if (proparallel[0] != PROPARALLEL_UNSAFE)
	{
		if (proparallel[0] == PROPARALLEL_SAFE)
			appendPQExpBufferStr(q, " PARALLEL SAFE");
		else if (proparallel[0] == PROPARALLEL_RESTRICTED)
			appendPQExpBufferStr(q, " PARALLEL RESTRICTED");
		else if (proparallel[0] != PROPARALLEL_UNSAFE)
			pg_fatal("unrecognized proparallel value for function \"%s\"",
					 finfo->dobj.name);
	}

	for (int i = 0; i < nconfigitems; i++)
	{
		/* we feel free to scribble on configitems[] here */
		char	   *configitem = configitems[i];
		char	   *pos;

		pos = strchr(configitem, '=');
		if (pos == NULL)
			continue;
		*pos++ = '\0';
		appendPQExpBuffer(q, "\n    SET %s TO ", fmtId(configitem));

		/*
		 * Variables that are marked GUC_LIST_QUOTE were already fully quoted
		 * by flatten_set_variable_args() before they were put into the
		 * proconfig array.  However, because the quoting rules used there
		 * aren't exactly like SQL's, we have to break the list value apart
		 * and then quote the elements as string literals.  (The elements may
		 * be double-quoted as-is, but we can't just feed them to the SQL
		 * parser; it would do the wrong thing with elements that are
		 * zero-length or longer than NAMEDATALEN.)
		 *
		 * Variables that are not so marked should just be emitted as simple
		 * string literals.  If the variable is not known to
		 * variable_is_guc_list_quote(), we'll do that; this makes it unsafe
		 * to use GUC_LIST_QUOTE for extension variables.
		 */
		if (variable_is_guc_list_quote(configitem))
		{
			char	  **namelist;
			char	  **nameptr;

			/* Parse string into list of identifiers */
			/* this shouldn't fail really */
			if (SplitGUCList(pos, ',', &namelist))
			{
				for (nameptr = namelist; *nameptr; nameptr++)
				{
					if (nameptr != namelist)
						appendPQExpBufferStr(q, ", ");
					appendStringLiteralAH(q, *nameptr, fout);
				}
			}
			pg_free(namelist);
		}
		else
			appendStringLiteralAH(q, pos, fout);
	}

	appendPQExpBuffer(q, "\n    %s;\n", asPart->data);

	append_depends_on_extension(fout, q, &finfo->dobj,
								"pg_catalog.pg_proc", keyword,
								qual_funcsig);

	

	if (finfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, finfo->dobj.catId, finfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = funcsig_tag,
								  .namespace = finfo->dobj.namespace->dobj.name,
								  .owner = finfo->rolname,
								  .description = keyword,
								  .section = finfo->postponed_def ?
								  SECTION_POST_DATA : SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delqry->data));

	/* Dump Function Comments and Security Labels */
	if (finfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, keyword, funcsig,
					finfo->dobj.namespace->dobj.name, finfo->rolname,
					finfo->dobj.catId, 0, finfo->dobj.dumpId);

	if (finfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, keyword, funcsig,
					 finfo->dobj.namespace->dobj.name, finfo->rolname,
					 finfo->dobj.catId, 0, finfo->dobj.dumpId);

	if (finfo->dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, finfo->dobj.dumpId, InvalidDumpId, keyword,
				funcsig, NULL,
				finfo->dobj.namespace->dobj.name,
				NULL, finfo->rolname, &finfo->dacl);

	PQclear(res);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delqry);
	destroyPQExpBuffer(asPart);
	free(funcsig);
	free(funcfullsig);
	free(funcsig_tag);
	free(qual_funcsig);
	free(configitems);
}


/*
 * Dump a user-defined cast
 */
static void
dumpCast(Archive *fout, const CastInfo *cast)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer defqry;
	PQExpBuffer delqry;
	PQExpBuffer labelq;
	PQExpBuffer castargs;
	FuncInfo   *funcInfo = NULL;
	const char *sourceType;
	const char *targetType;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	/* Cannot dump if we don't have the cast function's info */
	if (OidIsValid(cast->castfunc))
	{
		funcInfo = findFuncByOid(cast->castfunc);
		if (funcInfo == NULL)
			pg_fatal("could not find function definition for function with OID %u",
					 cast->castfunc);
	}

	defqry = createPQExpBuffer();
	delqry = createPQExpBuffer();
	labelq = createPQExpBuffer();
	castargs = createPQExpBuffer();

	sourceType = getFormattedTypeName(fout, cast->castsource, zeroAsNone);
	targetType = getFormattedTypeName(fout, cast->casttarget, zeroAsNone);
	appendPQExpBuffer(delqry, "DROP CAST (%s AS %s);\n",
					  sourceType, targetType);

	appendPQExpBuffer(defqry, "CREATE CAST (%s AS %s) ",
					  sourceType, targetType);

	switch (cast->castmethod)
	{
		case COERCION_METHOD_BINARY:
			appendPQExpBufferStr(defqry, "WITHOUT FUNCTION");
			break;
		case COERCION_METHOD_INOUT:
			appendPQExpBufferStr(defqry, "WITH INOUT");
			break;
		case COERCION_METHOD_FUNCTION:
			if (funcInfo)
			{
				char	   *fsig = format_function_signature(fout, funcInfo, true);

				/*
				 * Always qualify the function name (format_function_signature
				 * won't qualify it).
				 */
				appendPQExpBuffer(defqry, "WITH FUNCTION %s.%s",
								  fmtId(funcInfo->dobj.namespace->dobj.name), fsig);
				free(fsig);
			}
			else
				pg_log_warning("bogus value in pg_cast.castfunc or pg_cast.castmethod field");
			break;
		default:
			pg_log_warning("bogus value in pg_cast.castmethod field");
	}

	if (cast->castcontext == 'a')
		appendPQExpBufferStr(defqry, " AS ASSIGNMENT");
	else if (cast->castcontext == 'i')
		appendPQExpBufferStr(defqry, " AS IMPLICIT");
	appendPQExpBufferStr(defqry, ";\n");

	appendPQExpBuffer(labelq, "CAST (%s AS %s)",
					  sourceType, targetType);

	appendPQExpBuffer(castargs, "(%s AS %s)",
					  sourceType, targetType);

	

	if (cast->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, cast->dobj.catId, cast->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = labelq->data,
								  .description = "CAST",
								  .section = SECTION_PRE_DATA,
								  .createStmt = defqry->data,
								  .dropStmt = delqry->data));

	/* Dump Cast Comments */
	if (cast->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "CAST", castargs->data,
					NULL, "",
					cast->dobj.catId, 0, cast->dobj.dumpId);

	destroyPQExpBuffer(defqry);
	destroyPQExpBuffer(delqry);
	destroyPQExpBuffer(labelq);
	destroyPQExpBuffer(castargs);
}

/*
 * Dump a transform
 */
static void
dumpTransform(Archive *fout, const TransformInfo *transform)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer defqry;
	PQExpBuffer delqry;
	PQExpBuffer labelq;
	PQExpBuffer transformargs;
	FuncInfo   *fromsqlFuncInfo = NULL;
	FuncInfo   *tosqlFuncInfo = NULL;
	char	   *lanname;
	const char *transformType;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	/* Cannot dump if we don't have the transform functions' info */
	if (OidIsValid(transform->trffromsql))
	{
		fromsqlFuncInfo = findFuncByOid(transform->trffromsql);
		if (fromsqlFuncInfo == NULL)
			pg_fatal("could not find function definition for function with OID %u",
					 transform->trffromsql);
	}
	if (OidIsValid(transform->trftosql))
	{
		tosqlFuncInfo = findFuncByOid(transform->trftosql);
		if (tosqlFuncInfo == NULL)
			pg_fatal("could not find function definition for function with OID %u",
					 transform->trftosql);
	}

	defqry = createPQExpBuffer();
	delqry = createPQExpBuffer();
	labelq = createPQExpBuffer();
	transformargs = createPQExpBuffer();

	lanname = get_language_name(fout, transform->trflang);
	transformType = getFormattedTypeName(fout, transform->trftype, zeroAsNone);

	appendPQExpBuffer(delqry, "DROP TRANSFORM FOR %s LANGUAGE %s;\n",
					  transformType, lanname);

	appendPQExpBuffer(defqry, "CREATE TRANSFORM FOR %s LANGUAGE %s (",
					  transformType, lanname);

	if (!transform->trffromsql && !transform->trftosql)
		pg_log_warning("bogus transform definition, at least one of trffromsql and trftosql should be nonzero");

	if (transform->trffromsql)
	{
		if (fromsqlFuncInfo)
		{
			char	   *fsig = format_function_signature(fout, fromsqlFuncInfo, true);

			/*
			 * Always qualify the function name (format_function_signature
			 * won't qualify it).
			 */
			appendPQExpBuffer(defqry, "FROM SQL WITH FUNCTION %s.%s",
							  fmtId(fromsqlFuncInfo->dobj.namespace->dobj.name), fsig);
			free(fsig);
		}
		else
			pg_log_warning("bogus value in pg_transform.trffromsql field");
	}

	if (transform->trftosql)
	{
		if (transform->trffromsql)
			appendPQExpBufferStr(defqry, ", ");

		if (tosqlFuncInfo)
		{
			char	   *fsig = format_function_signature(fout, tosqlFuncInfo, true);

			/*
			 * Always qualify the function name (format_function_signature
			 * won't qualify it).
			 */
			appendPQExpBuffer(defqry, "TO SQL WITH FUNCTION %s.%s",
							  fmtId(tosqlFuncInfo->dobj.namespace->dobj.name), fsig);
			free(fsig);
		}
		else
			pg_log_warning("bogus value in pg_transform.trftosql field");
	}

	appendPQExpBufferStr(defqry, ");\n");

	appendPQExpBuffer(labelq, "TRANSFORM FOR %s LANGUAGE %s",
					  transformType, lanname);

	appendPQExpBuffer(transformargs, "FOR %s LANGUAGE %s",
					  transformType, lanname);

	

	if (transform->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, transform->dobj.catId, transform->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = labelq->data,
								  .description = "TRANSFORM",
								  .section = SECTION_PRE_DATA,
								  .createStmt = defqry->data,
								  .dropStmt = delqry->data,
								  .deps = transform->dobj.dependencies,
								  .nDeps = transform->dobj.nDeps));

	/* Dump Transform Comments */
	if (transform->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "TRANSFORM", transformargs->data,
					NULL, "",
					transform->dobj.catId, 0, transform->dobj.dumpId);

	free(lanname);
	destroyPQExpBuffer(defqry);
	destroyPQExpBuffer(delqry);
	destroyPQExpBuffer(labelq);
	destroyPQExpBuffer(transformargs);
}


/*
 * dumpOpr
 *	  write out a single operator definition
 */
static void
dumpOpr(Archive *fout, const OprInfo *oprinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer query;
	PQExpBuffer q;
	PQExpBuffer delq;
	PQExpBuffer oprid;
	PQExpBuffer details;
	PGresult   *res;
	int			i_oprkind;
	int			i_oprcode;
	int			i_oprleft;
	int			i_oprright;
	int			i_oprcom;
	int			i_oprnegate;
	int			i_oprrest;
	int			i_oprjoin;
	int			i_oprcanmerge;
	int			i_oprcanhash;
	char	   *oprkind;
	char	   *oprcode;
	char	   *oprleft;
	char	   *oprright;
	char	   *oprcom;
	char	   *oprnegate;
	char	   *oprrest;
	char	   *oprjoin;
	char	   *oprcanmerge;
	char	   *oprcanhash;
	char	   *oprregproc;
	char	   *oprref;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	/*
	 * some operators are invalid because they were the result of user
	 * defining operators before commutators exist
	 */
	if (!OidIsValid(oprinfo->oprcode))
		return;

	query = createPQExpBuffer();
	q = createPQExpBuffer();
	delq = createPQExpBuffer();
	oprid = createPQExpBuffer();
	details = createPQExpBuffer();

	if (!fout->is_prepared[PREPQUERY_DUMPOPR])
	{
		/* Set up query for operator-specific details */
		appendPQExpBufferStr(query,
							 "PREPARE dumpOpr(pg_catalog.oid) AS\n"
							 "SELECT oprkind, "
							 "oprcode::pg_catalog.regprocedure, "
							 "oprleft::pg_catalog.regtype, "
							 "oprright::pg_catalog.regtype, "
							 "oprcom, "
							 "oprnegate, "
							 "oprrest::pg_catalog.regprocedure, "
							 "oprjoin::pg_catalog.regprocedure, "
							 "oprcanmerge, oprcanhash "
							 "FROM pg_catalog.pg_operator "
							 "WHERE oid = $1");

		ExecuteSqlStatement(fout, query->data);

		fout->is_prepared[PREPQUERY_DUMPOPR] = true;
	}

	printfPQExpBuffer(query,
					  "EXECUTE dumpOpr('%u')",
					  oprinfo->dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	i_oprkind = PQfnumber(res, "oprkind");
	i_oprcode = PQfnumber(res, "oprcode");
	i_oprleft = PQfnumber(res, "oprleft");
	i_oprright = PQfnumber(res, "oprright");
	i_oprcom = PQfnumber(res, "oprcom");
	i_oprnegate = PQfnumber(res, "oprnegate");
	i_oprrest = PQfnumber(res, "oprrest");
	i_oprjoin = PQfnumber(res, "oprjoin");
	i_oprcanmerge = PQfnumber(res, "oprcanmerge");
	i_oprcanhash = PQfnumber(res, "oprcanhash");

	oprkind = PQgetvalue(res, 0, i_oprkind);
	oprcode = PQgetvalue(res, 0, i_oprcode);
	oprleft = PQgetvalue(res, 0, i_oprleft);
	oprright = PQgetvalue(res, 0, i_oprright);
	oprcom = PQgetvalue(res, 0, i_oprcom);
	oprnegate = PQgetvalue(res, 0, i_oprnegate);
	oprrest = PQgetvalue(res, 0, i_oprrest);
	oprjoin = PQgetvalue(res, 0, i_oprjoin);
	oprcanmerge = PQgetvalue(res, 0, i_oprcanmerge);
	oprcanhash = PQgetvalue(res, 0, i_oprcanhash);

	/* In PG14 upwards postfix operator support does not exist anymore. */
	if (strcmp(oprkind, "r") == 0)
		pg_log_warning("postfix operators are not supported anymore (operator \"%s\")",
					   oprcode);

	oprregproc = convertRegProcReference(oprcode);
	if (oprregproc)
	{
		appendPQExpBuffer(details, "    FUNCTION = %s", oprregproc);
		free(oprregproc);
	}

	appendPQExpBuffer(oprid, "%s (",
					  oprinfo->dobj.name);

	/*
	 * right unary means there's a left arg and left unary means there's a
	 * right arg.  (Although the "r" case is dead code for PG14 and later,
	 * continue to support it in case we're dumping from an old server.)
	 */
	if (strcmp(oprkind, "r") == 0 ||
		strcmp(oprkind, "b") == 0)
	{
		appendPQExpBuffer(details, ",\n    LEFTARG = %s", oprleft);
		appendPQExpBufferStr(oprid, oprleft);
	}
	else
		appendPQExpBufferStr(oprid, "NONE");

	if (strcmp(oprkind, "l") == 0 ||
		strcmp(oprkind, "b") == 0)
	{
		appendPQExpBuffer(details, ",\n    RIGHTARG = %s", oprright);
		appendPQExpBuffer(oprid, ", %s)", oprright);
	}
	else
		appendPQExpBufferStr(oprid, ", NONE)");

	oprref = getFormattedOperatorName(oprcom);
	if (oprref)
	{
		appendPQExpBuffer(details, ",\n    COMMUTATOR = %s", oprref);
		free(oprref);
	}

	oprref = getFormattedOperatorName(oprnegate);
	if (oprref)
	{
		appendPQExpBuffer(details, ",\n    NEGATOR = %s", oprref);
		free(oprref);
	}

	if (strcmp(oprcanmerge, "t") == 0)
		appendPQExpBufferStr(details, ",\n    MERGES");

	if (strcmp(oprcanhash, "t") == 0)
		appendPQExpBufferStr(details, ",\n    HASHES");

	oprregproc = convertRegProcReference(oprrest);
	if (oprregproc)
	{
		appendPQExpBuffer(details, ",\n    RESTRICT = %s", oprregproc);
		free(oprregproc);
	}

	oprregproc = convertRegProcReference(oprjoin);
	if (oprregproc)
	{
		appendPQExpBuffer(details, ",\n    JOIN = %s", oprregproc);
		free(oprregproc);
	}

	appendPQExpBuffer(delq, "DROP OPERATOR %s.%s;\n",
					  fmtId(oprinfo->dobj.namespace->dobj.name),
					  oprid->data);

	appendPQExpBuffer(q, "CREATE OPERATOR %s.%s (\n%s\n);\n",
					  fmtId(oprinfo->dobj.namespace->dobj.name),
					  oprinfo->dobj.name, details->data);

	

	if (oprinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, oprinfo->dobj.catId, oprinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = oprinfo->dobj.name,
								  .namespace = oprinfo->dobj.namespace->dobj.name,
								  .owner = oprinfo->rolname,
								  .description = "OPERATOR",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Operator Comments */
	if (oprinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "OPERATOR", oprid->data,
					oprinfo->dobj.namespace->dobj.name, oprinfo->rolname,
					oprinfo->dobj.catId, 0, oprinfo->dobj.dumpId);

	PQclear(res);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(oprid);
	destroyPQExpBuffer(details);
}

/*
 * Convert a function reference obtained from pg_operator
 *
 * Returns allocated string of what to print, or NULL if function references
 * is InvalidOid. Returned string is expected to be free'd by the caller.
 *
 * The input is a REGPROCEDURE display; we have to strip the argument-types
 * part.
 */
static char *
convertRegProcReference(const char *proc)
{
	char	   *name;
	char	   *paren;
	bool		inquote;

	/* In all cases "-" means a null reference */
	if (strcmp(proc, "-") == 0)
		return NULL;

	name = pg_strdup(proc);
	/* find non-double-quoted left paren */
	inquote = false;
	for (paren = name; *paren; paren++)
	{
		if (*paren == '(' && !inquote)
		{
			*paren = '\0';
			break;
		}
		if (*paren == '"')
			inquote = !inquote;
	}
	return name;
}

/*
 * getFormattedOperatorName - retrieve the operator name for the
 * given operator OID (presented in string form).
 *
 * Returns an allocated string, or NULL if the given OID is invalid.
 * Caller is responsible for free'ing result string.
 *
 * What we produce has the format "OPERATOR(schema.oprname)".  This is only
 * useful in commands where the operator's argument types can be inferred from
 * context.  We always schema-qualify the name, though.  The predecessor to
 * this code tried to skip the schema qualification if possible, but that led
 * to wrong results in corner cases, such as if an operator and its negator
 * are in different schemas.
 */
static char *
getFormattedOperatorName(const char *oproid)
{
	OprInfo    *oprInfo;

	/* In all cases "0" means a null reference */
	if (strcmp(oproid, "0") == 0)
		return NULL;

	oprInfo = findOprByOid(atooid(oproid));
	if (oprInfo == NULL)
	{
		pg_log_warning("could not find operator with OID %s",
					   oproid);
		return NULL;
	}

	return psprintf("OPERATOR(%s.%s)",
					fmtId(oprInfo->dobj.namespace->dobj.name),
					oprInfo->dobj.name);
}

/*
 * Convert a function OID obtained from pg_ts_parser or pg_ts_template
 *
 * It is sufficient to use REGPROC rather than REGPROCEDURE, since the
 * argument lists of these functions are predetermined.  Note that the
 * caller should ensure we are in the proper schema, because the results
 * are search path dependent!
 */
static char *
convertTSFunction(Archive *fout, Oid funcOid)
{
	char	   *result;
	char		query[128];
	PGresult   *res;

	snprintf(query, sizeof(query),
			 "SELECT '%u'::pg_catalog.regproc", funcOid);
	res = ExecuteSqlQueryForSingleRow(fout, query);

	result = pg_strdup(PQgetvalue(res, 0, 0));

	PQclear(res);

	return result;
}

/*
 * dumpAccessMethod
 *	  write out a single access method definition
 */
static void
dumpAccessMethod(Archive *fout, const AccessMethodInfo *aminfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PQExpBuffer delq;
	char	   *qamname;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();

	qamname = pg_strdup(fmtId(aminfo->dobj.name));

	appendPQExpBuffer(q, "CREATE ACCESS METHOD %s ", qamname);

	switch (aminfo->amtype)
	{
		case AMTYPE_INDEX:
			appendPQExpBufferStr(q, "TYPE INDEX ");
			break;
		case AMTYPE_TABLE:
			appendPQExpBufferStr(q, "TYPE TABLE ");
			break;
		default:
			pg_log_warning("invalid type \"%c\" of access method \"%s\"",
						   aminfo->amtype, qamname);
			destroyPQExpBuffer(q);
			destroyPQExpBuffer(delq);
			free(qamname);
			return;
	}

	appendPQExpBuffer(q, "HANDLER %s;\n", aminfo->amhandler);

	appendPQExpBuffer(delq, "DROP ACCESS METHOD %s;\n",
					  qamname);

	

	if (aminfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, aminfo->dobj.catId, aminfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = aminfo->dobj.name,
								  .description = "ACCESS METHOD",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Access Method Comments */
	if (aminfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "ACCESS METHOD", qamname,
					NULL, "",
					aminfo->dobj.catId, 0, aminfo->dobj.dumpId);

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	free(qamname);
}

/*
 * dumpOpclass
 *	  write out a single operator class definition
 */
static void
dumpOpclass(Archive *fout, const OpclassInfo *opcinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer query;
	PQExpBuffer q;
	PQExpBuffer delq;
	PQExpBuffer nameusing;
	PGresult   *res;
	int			ntups;
	int			i_opcintype;
	int			i_opckeytype;
	int			i_opcdefault;
	int			i_opcfamily;
	int			i_opcfamilyname;
	int			i_opcfamilynsp;
	int			i_amname;
	int			i_amopstrategy;
	int			i_amopopr;
	int			i_sortfamily;
	int			i_sortfamilynsp;
	int			i_amprocnum;
	int			i_amproc;
	int			i_amproclefttype;
	int			i_amprocrighttype;
	char	   *opcintype;
	char	   *opckeytype;
	char	   *opcdefault;
	char	   *opcfamily;
	char	   *opcfamilyname;
	char	   *opcfamilynsp;
	char	   *amname;
	char	   *amopstrategy;
	char	   *amopopr;
	char	   *sortfamily;
	char	   *sortfamilynsp;
	char	   *amprocnum;
	char	   *amproc;
	char	   *amproclefttype;
	char	   *amprocrighttype;
	bool		needComma;
	int			i;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	query = createPQExpBuffer();
	q = createPQExpBuffer();
	delq = createPQExpBuffer();
	nameusing = createPQExpBuffer();

	/* Get additional fields from the pg_opclass row */
	appendPQExpBuffer(query, "SELECT opcintype::pg_catalog.regtype, "
					  "opckeytype::pg_catalog.regtype, "
					  "opcdefault, opcfamily, "
					  "opfname AS opcfamilyname, "
					  "nspname AS opcfamilynsp, "
					  "(SELECT amname FROM pg_catalog.pg_am WHERE oid = opcmethod) AS amname "
					  "FROM pg_catalog.pg_opclass c "
					  "LEFT JOIN pg_catalog.pg_opfamily f ON f.oid = opcfamily "
					  "LEFT JOIN pg_catalog.pg_namespace n ON n.oid = opfnamespace "
					  "WHERE c.oid = '%u'::pg_catalog.oid",
					  opcinfo->dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	i_opcintype = PQfnumber(res, "opcintype");
	i_opckeytype = PQfnumber(res, "opckeytype");
	i_opcdefault = PQfnumber(res, "opcdefault");
	i_opcfamily = PQfnumber(res, "opcfamily");
	i_opcfamilyname = PQfnumber(res, "opcfamilyname");
	i_opcfamilynsp = PQfnumber(res, "opcfamilynsp");
	i_amname = PQfnumber(res, "amname");

	/* opcintype may still be needed after we PQclear res */
	opcintype = pg_strdup(PQgetvalue(res, 0, i_opcintype));
	opckeytype = PQgetvalue(res, 0, i_opckeytype);
	opcdefault = PQgetvalue(res, 0, i_opcdefault);
	/* opcfamily will still be needed after we PQclear res */
	opcfamily = pg_strdup(PQgetvalue(res, 0, i_opcfamily));
	opcfamilyname = PQgetvalue(res, 0, i_opcfamilyname);
	opcfamilynsp = PQgetvalue(res, 0, i_opcfamilynsp);
	/* amname will still be needed after we PQclear res */
	amname = pg_strdup(PQgetvalue(res, 0, i_amname));

	appendPQExpBuffer(delq, "DROP OPERATOR CLASS %s",
					  fmtQualifiedDumpable(opcinfo));
	appendPQExpBuffer(delq, " USING %s;\n",
					  fmtId(amname));

	/* Build the fixed portion of the CREATE command */
	appendPQExpBuffer(q, "CREATE OPERATOR CLASS %s\n    ",
					  fmtQualifiedDumpable(opcinfo));
	if (strcmp(opcdefault, "t") == 0)
		appendPQExpBufferStr(q, "DEFAULT ");
	appendPQExpBuffer(q, "FOR TYPE %s USING %s",
					  opcintype,
					  fmtId(amname));
	if (strlen(opcfamilyname) > 0)
	{
		appendPQExpBufferStr(q, " FAMILY ");
		appendPQExpBuffer(q, "%s.", fmtId(opcfamilynsp));
		appendPQExpBufferStr(q, fmtId(opcfamilyname));
	}
	appendPQExpBufferStr(q, " AS\n    ");

	needComma = false;

	if (strcmp(opckeytype, "-") != 0)
	{
		appendPQExpBuffer(q, "STORAGE %s",
						  opckeytype);
		needComma = true;
	}

	PQclear(res);

	/*
	 * Now fetch and print the OPERATOR entries (pg_amop rows).
	 *
	 * Print only those opfamily members that are tied to the opclass by
	 * pg_depend entries.
	 */
	resetPQExpBuffer(query);
	appendPQExpBuffer(query, "SELECT amopstrategy, "
					  "amopopr::pg_catalog.regoperator, "
					  "opfname AS sortfamily, "
					  "nspname AS sortfamilynsp "
					  "FROM pg_catalog.pg_amop ao JOIN pg_catalog.pg_depend ON "
					  "(classid = 'pg_catalog.pg_amop'::pg_catalog.regclass AND objid = ao.oid) "
					  "LEFT JOIN pg_catalog.pg_opfamily f ON f.oid = amopsortfamily "
					  "LEFT JOIN pg_catalog.pg_namespace n ON n.oid = opfnamespace "
					  "WHERE refclassid = 'pg_catalog.pg_opclass'::pg_catalog.regclass "
					  "AND refobjid = '%u'::pg_catalog.oid "
					  "AND amopfamily = '%s'::pg_catalog.oid "
					  "ORDER BY amopstrategy",
					  opcinfo->dobj.catId.oid,
					  opcfamily);

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_amopstrategy = PQfnumber(res, "amopstrategy");
	i_amopopr = PQfnumber(res, "amopopr");
	i_sortfamily = PQfnumber(res, "sortfamily");
	i_sortfamilynsp = PQfnumber(res, "sortfamilynsp");

	for (i = 0; i < ntups; i++)
	{
		amopstrategy = PQgetvalue(res, i, i_amopstrategy);
		amopopr = PQgetvalue(res, i, i_amopopr);
		sortfamily = PQgetvalue(res, i, i_sortfamily);
		sortfamilynsp = PQgetvalue(res, i, i_sortfamilynsp);

		if (needComma)
			appendPQExpBufferStr(q, " ,\n    ");

		appendPQExpBuffer(q, "OPERATOR %s %s",
						  amopstrategy, amopopr);

		if (strlen(sortfamily) > 0)
		{
			appendPQExpBufferStr(q, " FOR ORDER BY ");
			appendPQExpBuffer(q, "%s.", fmtId(sortfamilynsp));
			appendPQExpBufferStr(q, fmtId(sortfamily));
		}

		needComma = true;
	}

	PQclear(res);

	/*
	 * Now fetch and print the FUNCTION entries (pg_amproc rows).
	 *
	 * Print only those opfamily members that are tied to the opclass by
	 * pg_depend entries.
	 *
	 * We print the amproclefttype/amprocrighttype even though in most cases
	 * the backend could deduce the right values, because of the corner case
	 * of a btree sort support function for a cross-type comparison.
	 */
	resetPQExpBuffer(query);

	appendPQExpBuffer(query, "SELECT amprocnum, "
					  "amproc::pg_catalog.regprocedure, "
					  "amproclefttype::pg_catalog.regtype, "
					  "amprocrighttype::pg_catalog.regtype "
					  "FROM pg_catalog.pg_amproc ap, pg_catalog.pg_depend "
					  "WHERE refclassid = 'pg_catalog.pg_opclass'::pg_catalog.regclass "
					  "AND refobjid = '%u'::pg_catalog.oid "
					  "AND classid = 'pg_catalog.pg_amproc'::pg_catalog.regclass "
					  "AND objid = ap.oid "
					  "ORDER BY amprocnum",
					  opcinfo->dobj.catId.oid);

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);

	i_amprocnum = PQfnumber(res, "amprocnum");
	i_amproc = PQfnumber(res, "amproc");
	i_amproclefttype = PQfnumber(res, "amproclefttype");
	i_amprocrighttype = PQfnumber(res, "amprocrighttype");

	for (i = 0; i < ntups; i++)
	{
		amprocnum = PQgetvalue(res, i, i_amprocnum);
		amproc = PQgetvalue(res, i, i_amproc);
		amproclefttype = PQgetvalue(res, i, i_amproclefttype);
		amprocrighttype = PQgetvalue(res, i, i_amprocrighttype);

		if (needComma)
			appendPQExpBufferStr(q, " ,\n    ");

		appendPQExpBuffer(q, "FUNCTION %s", amprocnum);

		if (*amproclefttype && *amprocrighttype)
			appendPQExpBuffer(q, " (%s, %s)", amproclefttype, amprocrighttype);

		appendPQExpBuffer(q, " %s", amproc);

		needComma = true;
	}

	PQclear(res);

	/*
	 * If needComma is still false it means we haven't added anything after
	 * the AS keyword.  To avoid printing broken SQL, append a dummy STORAGE
	 * clause with the same datatype.  This isn't sanctioned by the
	 * documentation, but actually DefineOpClass will treat it as a no-op.
	 */
	if (!needComma)
		appendPQExpBuffer(q, "STORAGE %s", opcintype);

	appendPQExpBufferStr(q, ";\n");

	appendPQExpBufferStr(nameusing, fmtId(opcinfo->dobj.name));
	appendPQExpBuffer(nameusing, " USING %s",
					  fmtId(amname));

	

	if (opcinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, opcinfo->dobj.catId, opcinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = opcinfo->dobj.name,
								  .namespace = opcinfo->dobj.namespace->dobj.name,
								  .owner = opcinfo->rolname,
								  .description = "OPERATOR CLASS",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Operator Class Comments */
	if (opcinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "OPERATOR CLASS", nameusing->data,
					opcinfo->dobj.namespace->dobj.name, opcinfo->rolname,
					opcinfo->dobj.catId, 0, opcinfo->dobj.dumpId);

	free(opcintype);
	free(opcfamily);
	free(amname);
	destroyPQExpBuffer(query);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(nameusing);
}

/*
 * dumpOpfamily
 *	  write out a single operator family definition
 *
 * Note: this also dumps any "loose" operator members that aren't bound to a
 * specific opclass within the opfamily.
 */
static void
dumpOpfamily(Archive *fout, const OpfamilyInfo *opfinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer query;
	PQExpBuffer q;
	PQExpBuffer delq;
	PQExpBuffer nameusing;
	PGresult   *res;
	PGresult   *res_ops;
	PGresult   *res_procs;
	int			ntups;
	int			i_amname;
	int			i_amopstrategy;
	int			i_amopopr;
	int			i_sortfamily;
	int			i_sortfamilynsp;
	int			i_amprocnum;
	int			i_amproc;
	int			i_amproclefttype;
	int			i_amprocrighttype;
	char	   *amname;
	char	   *amopstrategy;
	char	   *amopopr;
	char	   *sortfamily;
	char	   *sortfamilynsp;
	char	   *amprocnum;
	char	   *amproc;
	char	   *amproclefttype;
	char	   *amprocrighttype;
	bool		needComma;
	int			i;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	query = createPQExpBuffer();
	q = createPQExpBuffer();
	delq = createPQExpBuffer();
	nameusing = createPQExpBuffer();

	/*
	 * Fetch only those opfamily members that are tied directly to the
	 * opfamily by pg_depend entries.
	 */
	appendPQExpBuffer(query, "SELECT amopstrategy, "
					  "amopopr::pg_catalog.regoperator, "
					  "opfname AS sortfamily, "
					  "nspname AS sortfamilynsp "
					  "FROM pg_catalog.pg_amop ao JOIN pg_catalog.pg_depend ON "
					  "(classid = 'pg_catalog.pg_amop'::pg_catalog.regclass AND objid = ao.oid) "
					  "LEFT JOIN pg_catalog.pg_opfamily f ON f.oid = amopsortfamily "
					  "LEFT JOIN pg_catalog.pg_namespace n ON n.oid = opfnamespace "
					  "WHERE refclassid = 'pg_catalog.pg_opfamily'::pg_catalog.regclass "
					  "AND refobjid = '%u'::pg_catalog.oid "
					  "AND amopfamily = '%u'::pg_catalog.oid "
					  "ORDER BY amopstrategy",
					  opfinfo->dobj.catId.oid,
					  opfinfo->dobj.catId.oid);

	res_ops = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	resetPQExpBuffer(query);

	appendPQExpBuffer(query, "SELECT amprocnum, "
					  "amproc::pg_catalog.regprocedure, "
					  "amproclefttype::pg_catalog.regtype, "
					  "amprocrighttype::pg_catalog.regtype "
					  "FROM pg_catalog.pg_amproc ap, pg_catalog.pg_depend "
					  "WHERE refclassid = 'pg_catalog.pg_opfamily'::pg_catalog.regclass "
					  "AND refobjid = '%u'::pg_catalog.oid "
					  "AND classid = 'pg_catalog.pg_amproc'::pg_catalog.regclass "
					  "AND objid = ap.oid "
					  "ORDER BY amprocnum",
					  opfinfo->dobj.catId.oid);

	res_procs = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	/* Get additional fields from the pg_opfamily row */
	resetPQExpBuffer(query);

	appendPQExpBuffer(query, "SELECT "
					  "(SELECT amname FROM pg_catalog.pg_am WHERE oid = opfmethod) AS amname "
					  "FROM pg_catalog.pg_opfamily "
					  "WHERE oid = '%u'::pg_catalog.oid",
					  opfinfo->dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	i_amname = PQfnumber(res, "amname");

	/* amname will still be needed after we PQclear res */
	amname = pg_strdup(PQgetvalue(res, 0, i_amname));

	appendPQExpBuffer(delq, "DROP OPERATOR FAMILY %s",
					  fmtQualifiedDumpable(opfinfo));
	appendPQExpBuffer(delq, " USING %s;\n",
					  fmtId(amname));

	/* Build the fixed portion of the CREATE command */
	appendPQExpBuffer(q, "CREATE OPERATOR FAMILY %s",
					  fmtQualifiedDumpable(opfinfo));
	appendPQExpBuffer(q, " USING %s;\n",
					  fmtId(amname));

	PQclear(res);

	/* Do we need an ALTER to add loose members? */
	if (PQntuples(res_ops) > 0 || PQntuples(res_procs) > 0)
	{
		appendPQExpBuffer(q, "ALTER OPERATOR FAMILY %s",
						  fmtQualifiedDumpable(opfinfo));
		appendPQExpBuffer(q, " USING %s ADD\n    ",
						  fmtId(amname));

		needComma = false;

		/*
		 * Now fetch and print the OPERATOR entries (pg_amop rows).
		 */
		ntups = PQntuples(res_ops);

		i_amopstrategy = PQfnumber(res_ops, "amopstrategy");
		i_amopopr = PQfnumber(res_ops, "amopopr");
		i_sortfamily = PQfnumber(res_ops, "sortfamily");
		i_sortfamilynsp = PQfnumber(res_ops, "sortfamilynsp");

		for (i = 0; i < ntups; i++)
		{
			amopstrategy = PQgetvalue(res_ops, i, i_amopstrategy);
			amopopr = PQgetvalue(res_ops, i, i_amopopr);
			sortfamily = PQgetvalue(res_ops, i, i_sortfamily);
			sortfamilynsp = PQgetvalue(res_ops, i, i_sortfamilynsp);

			if (needComma)
				appendPQExpBufferStr(q, " ,\n    ");

			appendPQExpBuffer(q, "OPERATOR %s %s",
							  amopstrategy, amopopr);

			if (strlen(sortfamily) > 0)
			{
				appendPQExpBufferStr(q, " FOR ORDER BY ");
				appendPQExpBuffer(q, "%s.", fmtId(sortfamilynsp));
				appendPQExpBufferStr(q, fmtId(sortfamily));
			}

			needComma = true;
		}

		/*
		 * Now fetch and print the FUNCTION entries (pg_amproc rows).
		 */
		ntups = PQntuples(res_procs);

		i_amprocnum = PQfnumber(res_procs, "amprocnum");
		i_amproc = PQfnumber(res_procs, "amproc");
		i_amproclefttype = PQfnumber(res_procs, "amproclefttype");
		i_amprocrighttype = PQfnumber(res_procs, "amprocrighttype");

		for (i = 0; i < ntups; i++)
		{
			amprocnum = PQgetvalue(res_procs, i, i_amprocnum);
			amproc = PQgetvalue(res_procs, i, i_amproc);
			amproclefttype = PQgetvalue(res_procs, i, i_amproclefttype);
			amprocrighttype = PQgetvalue(res_procs, i, i_amprocrighttype);

			if (needComma)
				appendPQExpBufferStr(q, " ,\n    ");

			appendPQExpBuffer(q, "FUNCTION %s (%s, %s) %s",
							  amprocnum, amproclefttype, amprocrighttype,
							  amproc);

			needComma = true;
		}

		appendPQExpBufferStr(q, ";\n");
	}

	appendPQExpBufferStr(nameusing, fmtId(opfinfo->dobj.name));
	appendPQExpBuffer(nameusing, " USING %s",
					  fmtId(amname));

	

	if (opfinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, opfinfo->dobj.catId, opfinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = opfinfo->dobj.name,
								  .namespace = opfinfo->dobj.namespace->dobj.name,
								  .owner = opfinfo->rolname,
								  .description = "OPERATOR FAMILY",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Operator Family Comments */
	if (opfinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "OPERATOR FAMILY", nameusing->data,
					opfinfo->dobj.namespace->dobj.name, opfinfo->rolname,
					opfinfo->dobj.catId, 0, opfinfo->dobj.dumpId);

	free(amname);
	PQclear(res_ops);
	PQclear(res_procs);
	destroyPQExpBuffer(query);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(nameusing);
}

/*
 * dumpCollation
 *	  write out a single collation definition
 */
static void
dumpCollation(Archive *fout, const CollInfo *collinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer query;
	PQExpBuffer q;
	PQExpBuffer delq;
	char	   *qcollname;
	PGresult   *res;
	int			i_collprovider;
	int			i_collisdeterministic;
	int			i_collcollate;
	int			i_collctype;
	int			i_colllocale;
	int			i_collicurules;
	const char *collprovider;
	const char *collcollate;
	const char *collctype;
	const char *colllocale;
	const char *collicurules;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	query = createPQExpBuffer();
	q = createPQExpBuffer();
	delq = createPQExpBuffer();

	qcollname = pg_strdup(fmtId(collinfo->dobj.name));

	/* Get collation-specific details */
	appendPQExpBufferStr(query, "SELECT ");

	if (fout->remoteVersion >= 100000)
		appendPQExpBufferStr(query,
							 "collprovider, "
							 "collversion, ");
	else
		appendPQExpBufferStr(query,
							 "'c' AS collprovider, "
							 "NULL AS collversion, ");

	if (fout->remoteVersion >= 120000)
		appendPQExpBufferStr(query,
							 "collisdeterministic, ");
	else
		appendPQExpBufferStr(query,
							 "true AS collisdeterministic, ");

	if (fout->remoteVersion >= 170000)
		appendPQExpBufferStr(query,
							 "colllocale, ");
	else if (fout->remoteVersion >= 150000)
		appendPQExpBufferStr(query,
							 "colliculocale AS colllocale, ");
	else
		appendPQExpBufferStr(query,
							 "NULL AS colllocale, ");

	if (fout->remoteVersion >= 160000)
		appendPQExpBufferStr(query,
							 "collicurules, ");
	else
		appendPQExpBufferStr(query,
							 "NULL AS collicurules, ");

	appendPQExpBuffer(query,
					  "collcollate, "
					  "collctype "
					  "FROM pg_catalog.pg_collation c "
					  "WHERE c.oid = '%u'::pg_catalog.oid",
					  collinfo->dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	i_collprovider = PQfnumber(res, "collprovider");
	i_collisdeterministic = PQfnumber(res, "collisdeterministic");
	i_collcollate = PQfnumber(res, "collcollate");
	i_collctype = PQfnumber(res, "collctype");
	i_colllocale = PQfnumber(res, "colllocale");
	i_collicurules = PQfnumber(res, "collicurules");

	collprovider = PQgetvalue(res, 0, i_collprovider);

	if (!PQgetisnull(res, 0, i_collcollate))
		collcollate = PQgetvalue(res, 0, i_collcollate);
	else
		collcollate = NULL;

	if (!PQgetisnull(res, 0, i_collctype))
		collctype = PQgetvalue(res, 0, i_collctype);
	else
		collctype = NULL;

	/*
	 * Before version 15, collcollate and collctype were of type NAME and
	 * non-nullable. Treat empty strings as NULL for consistency.
	 */
	if (fout->remoteVersion < 150000)
	{
		if (collcollate[0] == '\0')
			collcollate = NULL;
		if (collctype[0] == '\0')
			collctype = NULL;
	}

	if (!PQgetisnull(res, 0, i_colllocale))
		colllocale = PQgetvalue(res, 0, i_colllocale);
	else
		colllocale = NULL;

	if (!PQgetisnull(res, 0, i_collicurules))
		collicurules = PQgetvalue(res, 0, i_collicurules);
	else
		collicurules = NULL;

	appendPQExpBuffer(delq, "DROP COLLATION %s;\n",
					  fmtQualifiedDumpable(collinfo));

	appendPQExpBuffer(q, "CREATE COLLATION %s (",
					  fmtQualifiedDumpable(collinfo));

	appendPQExpBufferStr(q, "provider = ");
	if (collprovider[0] == 'b')
		appendPQExpBufferStr(q, "builtin");
	else if (collprovider[0] == 'c')
		appendPQExpBufferStr(q, "libc");
	else if (collprovider[0] == 'i')
		appendPQExpBufferStr(q, "icu");
	else if (collprovider[0] == 'd')
		/* to allow dumping pg_catalog; not accepted on input */
		appendPQExpBufferStr(q, "default");
	else
		pg_fatal("unrecognized collation provider: %s",
				 collprovider);

	if (strcmp(PQgetvalue(res, 0, i_collisdeterministic), "f") == 0)
		appendPQExpBufferStr(q, ", deterministic = false");

	if (collprovider[0] == 'd')
	{
		if (collcollate || collctype || colllocale || collicurules)
			pg_log_warning("invalid collation \"%s\"", qcollname);

		/* no locale -- the default collation cannot be reloaded anyway */
	}
	else if (collprovider[0] == 'b')
	{
		if (collcollate || collctype || !colllocale || collicurules)
			pg_log_warning("invalid collation \"%s\"", qcollname);

		appendPQExpBufferStr(q, ", locale = ");
		appendStringLiteralAH(q, colllocale ? colllocale : "",
							  fout);
	}
	else if (collprovider[0] == 'i')
	{
		if (fout->remoteVersion >= 150000)
		{
			if (collcollate || collctype || !colllocale)
				pg_log_warning("invalid collation \"%s\"", qcollname);

			appendPQExpBufferStr(q, ", locale = ");
			appendStringLiteralAH(q, colllocale ? colllocale : "",
								  fout);
		}
		else
		{
			if (!collcollate || !collctype || colllocale ||
				strcmp(collcollate, collctype) != 0)
				pg_log_warning("invalid collation \"%s\"", qcollname);

			appendPQExpBufferStr(q, ", locale = ");
			appendStringLiteralAH(q, collcollate ? collcollate : "", fout);
		}

		if (collicurules)
		{
			appendPQExpBufferStr(q, ", rules = ");
			appendStringLiteralAH(q, collicurules ? collicurules : "", fout);
		}
	}
	else if (collprovider[0] == 'c')
	{
		if (colllocale || collicurules || !collcollate || !collctype)
			pg_log_warning("invalid collation \"%s\"", qcollname);

		if (collcollate && collctype && strcmp(collcollate, collctype) == 0)
		{
			appendPQExpBufferStr(q, ", locale = ");
			appendStringLiteralAH(q, collcollate ? collcollate : "", fout);
		}
		else
		{
			appendPQExpBufferStr(q, ", lc_collate = ");
			appendStringLiteralAH(q, collcollate ? collcollate : "", fout);
			appendPQExpBufferStr(q, ", lc_ctype = ");
			appendStringLiteralAH(q, collctype ? collctype : "", fout);
		}
	}
	else
		pg_fatal("unrecognized collation provider: %s", collprovider);

	/*
	 * For binary upgrade, carry over the collation version.  For normal
	 * dump/restore, omit the version, so that it is computed upon restore.
	 */
	if (dopt->binary_upgrade)
	{
		int			i_collversion;

		i_collversion = PQfnumber(res, "collversion");
		if (!PQgetisnull(res, 0, i_collversion))
		{
			appendPQExpBufferStr(q, ", version = ");
			appendStringLiteralAH(q,
								  PQgetvalue(res, 0, i_collversion),
								  fout);
		}
	}

	appendPQExpBufferStr(q, ");\n");

	

	if (collinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, collinfo->dobj.catId, collinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = collinfo->dobj.name,
								  .namespace = collinfo->dobj.namespace->dobj.name,
								  .owner = collinfo->rolname,
								  .description = "COLLATION",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Collation Comments */
	if (collinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "COLLATION", qcollname,
					collinfo->dobj.namespace->dobj.name, collinfo->rolname,
					collinfo->dobj.catId, 0, collinfo->dobj.dumpId);

	PQclear(res);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	free(qcollname);
}

/*
 * dumpConversion
 *	  write out a single conversion definition
 */
static void
dumpConversion(Archive *fout, const ConvInfo *convinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer query;
	PQExpBuffer q;
	PQExpBuffer delq;
	char	   *qconvname;
	PGresult   *res;
	int			i_conforencoding;
	int			i_contoencoding;
	int			i_conproc;
	int			i_condefault;
	const char *conforencoding;
	const char *contoencoding;
	const char *conproc;
	bool		condefault;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	query = createPQExpBuffer();
	q = createPQExpBuffer();
	delq = createPQExpBuffer();

	qconvname = pg_strdup(fmtId(convinfo->dobj.name));

	/* Get conversion-specific details */
	appendPQExpBuffer(query, "SELECT "
					  "pg_catalog.pg_encoding_to_char(conforencoding) AS conforencoding, "
					  "pg_catalog.pg_encoding_to_char(contoencoding) AS contoencoding, "
					  "conproc, condefault "
					  "FROM pg_catalog.pg_conversion c "
					  "WHERE c.oid = '%u'::pg_catalog.oid",
					  convinfo->dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	i_conforencoding = PQfnumber(res, "conforencoding");
	i_contoencoding = PQfnumber(res, "contoencoding");
	i_conproc = PQfnumber(res, "conproc");
	i_condefault = PQfnumber(res, "condefault");

	conforencoding = PQgetvalue(res, 0, i_conforencoding);
	contoencoding = PQgetvalue(res, 0, i_contoencoding);
	conproc = PQgetvalue(res, 0, i_conproc);
	condefault = (PQgetvalue(res, 0, i_condefault)[0] == 't');

	appendPQExpBuffer(delq, "DROP CONVERSION %s;\n",
					  fmtQualifiedDumpable(convinfo));

	appendPQExpBuffer(q, "CREATE %sCONVERSION %s FOR ",
					  (condefault) ? "DEFAULT " : "",
					  fmtQualifiedDumpable(convinfo));
	appendStringLiteralAH(q, conforencoding, fout);
	appendPQExpBufferStr(q, " TO ");
	appendStringLiteralAH(q, contoencoding, fout);
	/* regproc output is already sufficiently quoted */
	appendPQExpBuffer(q, " FROM %s;\n", conproc);

	

	if (convinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, convinfo->dobj.catId, convinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = convinfo->dobj.name,
								  .namespace = convinfo->dobj.namespace->dobj.name,
								  .owner = convinfo->rolname,
								  .description = "CONVERSION",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Conversion Comments */
	if (convinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "CONVERSION", qconvname,
					convinfo->dobj.namespace->dobj.name, convinfo->rolname,
					convinfo->dobj.catId, 0, convinfo->dobj.dumpId);

	PQclear(res);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	free(qconvname);
}

/*
 * format_aggregate_signature: generate aggregate name and argument list
 *
 * The argument type names are qualified if needed.  The aggregate name
 * is never qualified.
 */
static char *
format_aggregate_signature(const AggInfo *agginfo, Archive *fout, bool honor_quotes)
{
	PQExpBufferData buf;
	int			j;

	initPQExpBuffer(&buf);
	if (honor_quotes)
		appendPQExpBufferStr(&buf, fmtId(agginfo->aggfn.dobj.name));
	else
		appendPQExpBufferStr(&buf, agginfo->aggfn.dobj.name);

	if (agginfo->aggfn.nargs == 0)
		appendPQExpBufferStr(&buf, "(*)");
	else
	{
		appendPQExpBufferChar(&buf, '(');
		for (j = 0; j < agginfo->aggfn.nargs; j++)
			appendPQExpBuffer(&buf, "%s%s",
							  (j > 0) ? ", " : "",
							  getFormattedTypeName(fout,
												   agginfo->aggfn.argtypes[j],
												   zeroIsError));
		appendPQExpBufferChar(&buf, ')');
	}
	return buf.data;
}

/*
 * dumpAgg
 *	  write out a single aggregate definition
 */
static void
dumpAgg(Archive *fout, const AggInfo *agginfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer query;
	PQExpBuffer q;
	PQExpBuffer delq;
	PQExpBuffer details;
	char	   *aggsig;			/* identity signature */
	char	   *aggfullsig = NULL;	/* full signature */
	char	   *aggsig_tag;
	PGresult   *res;
	int			i_agginitval;
	int			i_aggminitval;
	const char *aggtransfn;
	const char *aggfinalfn;
	const char *aggcombinefn;
	const char *aggserialfn;
	const char *aggdeserialfn;
	const char *aggmtransfn;
	const char *aggminvtransfn;
	const char *aggmfinalfn;
	bool		aggfinalextra;
	bool		aggmfinalextra;
	char		aggfinalmodify;
	char		aggmfinalmodify;
	const char *aggsortop;
	char	   *aggsortconvop;
	char		aggkind;
	const char *aggtranstype;
	const char *aggtransspace;
	const char *aggmtranstype;
	const char *aggmtransspace;
	const char *agginitval;
	const char *aggminitval;
	const char *proparallel;
	char		defaultfinalmodify;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	query = createPQExpBuffer();
	q = createPQExpBuffer();
	delq = createPQExpBuffer();
	details = createPQExpBuffer();

	if (!fout->is_prepared[PREPQUERY_DUMPAGG])
	{
		/* Set up query for aggregate-specific details */
		appendPQExpBufferStr(query,
							 "PREPARE dumpAgg(pg_catalog.oid) AS\n");

		appendPQExpBufferStr(query,
							 "SELECT "
							 "aggtransfn,\n"
							 "aggfinalfn,\n"
							 "aggtranstype::pg_catalog.regtype,\n"
							 "agginitval,\n"
							 "aggsortop,\n"
							 "pg_catalog.pg_get_function_arguments(p.oid) AS funcargs,\n"
							 "pg_catalog.pg_get_function_identity_arguments(p.oid) AS funciargs,\n");

		if (fout->remoteVersion >= 90400)
			appendPQExpBufferStr(query,
								 "aggkind,\n"
								 "aggmtransfn,\n"
								 "aggminvtransfn,\n"
								 "aggmfinalfn,\n"
								 "aggmtranstype::pg_catalog.regtype,\n"
								 "aggfinalextra,\n"
								 "aggmfinalextra,\n"
								 "aggtransspace,\n"
								 "aggmtransspace,\n"
								 "aggminitval,\n");
		else
			appendPQExpBufferStr(query,
								 "'n' AS aggkind,\n"
								 "'-' AS aggmtransfn,\n"
								 "'-' AS aggminvtransfn,\n"
								 "'-' AS aggmfinalfn,\n"
								 "0 AS aggmtranstype,\n"
								 "false AS aggfinalextra,\n"
								 "false AS aggmfinalextra,\n"
								 "0 AS aggtransspace,\n"
								 "0 AS aggmtransspace,\n"
								 "NULL AS aggminitval,\n");

		if (fout->remoteVersion >= 90600)
			appendPQExpBufferStr(query,
								 "aggcombinefn,\n"
								 "aggserialfn,\n"
								 "aggdeserialfn,\n"
								 "proparallel,\n");
		else
			appendPQExpBufferStr(query,
								 "'-' AS aggcombinefn,\n"
								 "'-' AS aggserialfn,\n"
								 "'-' AS aggdeserialfn,\n"
								 "'u' AS proparallel,\n");

		if (fout->remoteVersion >= 110000)
			appendPQExpBufferStr(query,
								 "aggfinalmodify,\n"
								 "aggmfinalmodify\n");
		else
			appendPQExpBufferStr(query,
								 "'0' AS aggfinalmodify,\n"
								 "'0' AS aggmfinalmodify\n");

		appendPQExpBufferStr(query,
							 "FROM pg_catalog.pg_aggregate a, pg_catalog.pg_proc p "
							 "WHERE a.aggfnoid = p.oid "
							 "AND p.oid = $1");

		ExecuteSqlStatement(fout, query->data);

		fout->is_prepared[PREPQUERY_DUMPAGG] = true;
	}

	printfPQExpBuffer(query,
					  "EXECUTE dumpAgg('%u')",
					  agginfo->aggfn.dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	i_agginitval = PQfnumber(res, "agginitval");
	i_aggminitval = PQfnumber(res, "aggminitval");

	aggtransfn = PQgetvalue(res, 0, PQfnumber(res, "aggtransfn"));
	aggfinalfn = PQgetvalue(res, 0, PQfnumber(res, "aggfinalfn"));
	aggcombinefn = PQgetvalue(res, 0, PQfnumber(res, "aggcombinefn"));
	aggserialfn = PQgetvalue(res, 0, PQfnumber(res, "aggserialfn"));
	aggdeserialfn = PQgetvalue(res, 0, PQfnumber(res, "aggdeserialfn"));
	aggmtransfn = PQgetvalue(res, 0, PQfnumber(res, "aggmtransfn"));
	aggminvtransfn = PQgetvalue(res, 0, PQfnumber(res, "aggminvtransfn"));
	aggmfinalfn = PQgetvalue(res, 0, PQfnumber(res, "aggmfinalfn"));
	aggfinalextra = (PQgetvalue(res, 0, PQfnumber(res, "aggfinalextra"))[0] == 't');
	aggmfinalextra = (PQgetvalue(res, 0, PQfnumber(res, "aggmfinalextra"))[0] == 't');
	aggfinalmodify = PQgetvalue(res, 0, PQfnumber(res, "aggfinalmodify"))[0];
	aggmfinalmodify = PQgetvalue(res, 0, PQfnumber(res, "aggmfinalmodify"))[0];
	aggsortop = PQgetvalue(res, 0, PQfnumber(res, "aggsortop"));
	aggkind = PQgetvalue(res, 0, PQfnumber(res, "aggkind"))[0];
	aggtranstype = PQgetvalue(res, 0, PQfnumber(res, "aggtranstype"));
	aggtransspace = PQgetvalue(res, 0, PQfnumber(res, "aggtransspace"));
	aggmtranstype = PQgetvalue(res, 0, PQfnumber(res, "aggmtranstype"));
	aggmtransspace = PQgetvalue(res, 0, PQfnumber(res, "aggmtransspace"));
	agginitval = PQgetvalue(res, 0, i_agginitval);
	aggminitval = PQgetvalue(res, 0, i_aggminitval);
	proparallel = PQgetvalue(res, 0, PQfnumber(res, "proparallel"));

	{
		char	   *funcargs;
		char	   *funciargs;

		funcargs = PQgetvalue(res, 0, PQfnumber(res, "funcargs"));
		funciargs = PQgetvalue(res, 0, PQfnumber(res, "funciargs"));
		aggfullsig = format_function_arguments(&agginfo->aggfn, funcargs, true);
		aggsig = format_function_arguments(&agginfo->aggfn, funciargs, true);
	}

	aggsig_tag = format_aggregate_signature(agginfo, fout, false);

	/* identify default modify flag for aggkind (must match DefineAggregate) */
	defaultfinalmodify = (aggkind == AGGKIND_NORMAL) ? AGGMODIFY_READ_ONLY : AGGMODIFY_READ_WRITE;
	/* replace omitted flags for old versions */
	if (aggfinalmodify == '0')
		aggfinalmodify = defaultfinalmodify;
	if (aggmfinalmodify == '0')
		aggmfinalmodify = defaultfinalmodify;

	/* regproc and regtype output is already sufficiently quoted */
	appendPQExpBuffer(details, "    SFUNC = %s,\n    STYPE = %s",
					  aggtransfn, aggtranstype);

	if (strcmp(aggtransspace, "0") != 0)
	{
		appendPQExpBuffer(details, ",\n    SSPACE = %s",
						  aggtransspace);
	}

	if (!PQgetisnull(res, 0, i_agginitval))
	{
		appendPQExpBufferStr(details, ",\n    INITCOND = ");
		appendStringLiteralAH(details, agginitval, fout);
	}

	if (strcmp(aggfinalfn, "-") != 0)
	{
		appendPQExpBuffer(details, ",\n    FINALFUNC = %s",
						  aggfinalfn);
		if (aggfinalextra)
			appendPQExpBufferStr(details, ",\n    FINALFUNC_EXTRA");
		if (aggfinalmodify != defaultfinalmodify)
		{
			switch (aggfinalmodify)
			{
				case AGGMODIFY_READ_ONLY:
					appendPQExpBufferStr(details, ",\n    FINALFUNC_MODIFY = READ_ONLY");
					break;
				case AGGMODIFY_SHAREABLE:
					appendPQExpBufferStr(details, ",\n    FINALFUNC_MODIFY = SHAREABLE");
					break;
				case AGGMODIFY_READ_WRITE:
					appendPQExpBufferStr(details, ",\n    FINALFUNC_MODIFY = READ_WRITE");
					break;
				default:
					pg_fatal("unrecognized aggfinalmodify value for aggregate \"%s\"",
							 agginfo->aggfn.dobj.name);
					break;
			}
		}
	}

	if (strcmp(aggcombinefn, "-") != 0)
		appendPQExpBuffer(details, ",\n    COMBINEFUNC = %s", aggcombinefn);

	if (strcmp(aggserialfn, "-") != 0)
		appendPQExpBuffer(details, ",\n    SERIALFUNC = %s", aggserialfn);

	if (strcmp(aggdeserialfn, "-") != 0)
		appendPQExpBuffer(details, ",\n    DESERIALFUNC = %s", aggdeserialfn);

	if (strcmp(aggmtransfn, "-") != 0)
	{
		appendPQExpBuffer(details, ",\n    MSFUNC = %s,\n    MINVFUNC = %s,\n    MSTYPE = %s",
						  aggmtransfn,
						  aggminvtransfn,
						  aggmtranstype);
	}

	if (strcmp(aggmtransspace, "0") != 0)
	{
		appendPQExpBuffer(details, ",\n    MSSPACE = %s",
						  aggmtransspace);
	}

	if (!PQgetisnull(res, 0, i_aggminitval))
	{
		appendPQExpBufferStr(details, ",\n    MINITCOND = ");
		appendStringLiteralAH(details, aggminitval, fout);
	}

	if (strcmp(aggmfinalfn, "-") != 0)
	{
		appendPQExpBuffer(details, ",\n    MFINALFUNC = %s",
						  aggmfinalfn);
		if (aggmfinalextra)
			appendPQExpBufferStr(details, ",\n    MFINALFUNC_EXTRA");
		if (aggmfinalmodify != defaultfinalmodify)
		{
			switch (aggmfinalmodify)
			{
				case AGGMODIFY_READ_ONLY:
					appendPQExpBufferStr(details, ",\n    MFINALFUNC_MODIFY = READ_ONLY");
					break;
				case AGGMODIFY_SHAREABLE:
					appendPQExpBufferStr(details, ",\n    MFINALFUNC_MODIFY = SHAREABLE");
					break;
				case AGGMODIFY_READ_WRITE:
					appendPQExpBufferStr(details, ",\n    MFINALFUNC_MODIFY = READ_WRITE");
					break;
				default:
					pg_fatal("unrecognized aggmfinalmodify value for aggregate \"%s\"",
							 agginfo->aggfn.dobj.name);
					break;
			}
		}
	}

	aggsortconvop = getFormattedOperatorName(aggsortop);
	if (aggsortconvop)
	{
		appendPQExpBuffer(details, ",\n    SORTOP = %s",
						  aggsortconvop);
		free(aggsortconvop);
	}

	if (aggkind == AGGKIND_HYPOTHETICAL)
		appendPQExpBufferStr(details, ",\n    HYPOTHETICAL");

	if (proparallel[0] != PROPARALLEL_UNSAFE)
	{
		if (proparallel[0] == PROPARALLEL_SAFE)
			appendPQExpBufferStr(details, ",\n    PARALLEL = safe");
		else if (proparallel[0] == PROPARALLEL_RESTRICTED)
			appendPQExpBufferStr(details, ",\n    PARALLEL = restricted");
		else if (proparallel[0] != PROPARALLEL_UNSAFE)
			pg_fatal("unrecognized proparallel value for function \"%s\"",
					 agginfo->aggfn.dobj.name);
	}

	appendPQExpBuffer(delq, "DROP AGGREGATE %s.%s;\n",
					  fmtId(agginfo->aggfn.dobj.namespace->dobj.name),
					  aggsig);

	appendPQExpBuffer(q, "CREATE AGGREGATE %s.%s (\n%s\n);\n",
					  fmtId(agginfo->aggfn.dobj.namespace->dobj.name),
					  aggfullsig ? aggfullsig : aggsig, details->data);

	

	if (agginfo->aggfn.dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, agginfo->aggfn.dobj.catId,
					 agginfo->aggfn.dobj.dumpId,
					 ARCHIVE_OPTS(.tag = aggsig_tag,
								  .namespace = agginfo->aggfn.dobj.namespace->dobj.name,
								  .owner = agginfo->aggfn.rolname,
								  .description = "AGGREGATE",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Aggregate Comments */
	if (agginfo->aggfn.dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "AGGREGATE", aggsig,
					agginfo->aggfn.dobj.namespace->dobj.name,
					agginfo->aggfn.rolname,
					agginfo->aggfn.dobj.catId, 0, agginfo->aggfn.dobj.dumpId);

	if (agginfo->aggfn.dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "AGGREGATE", aggsig,
					 agginfo->aggfn.dobj.namespace->dobj.name,
					 agginfo->aggfn.rolname,
					 agginfo->aggfn.dobj.catId, 0, agginfo->aggfn.dobj.dumpId);

	/*
	 * Since there is no GRANT ON AGGREGATE syntax, we have to make the ACL
	 * command look like a function's GRANT; in particular this affects the
	 * syntax for zero-argument aggregates and ordered-set aggregates.
	 */
	free(aggsig);

	aggsig = format_function_signature(fout, &agginfo->aggfn, true);

	if (agginfo->aggfn.dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, agginfo->aggfn.dobj.dumpId, InvalidDumpId,
				"FUNCTION", aggsig, NULL,
				agginfo->aggfn.dobj.namespace->dobj.name,
				NULL, agginfo->aggfn.rolname, &agginfo->aggfn.dacl);

	free(aggsig);
	free(aggfullsig);
	free(aggsig_tag);

	PQclear(res);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(details);
}

/*
 * dumpTSParser
 *	  write out a single text search parser
 */
static void
dumpTSParser(Archive *fout, const TSParserInfo *prsinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PQExpBuffer delq;
	char	   *qprsname;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();

	qprsname = pg_strdup(fmtId(prsinfo->dobj.name));

	appendPQExpBuffer(q, "CREATE TEXT SEARCH PARSER %s (\n",
					  fmtQualifiedDumpable(prsinfo));

	appendPQExpBuffer(q, "    START = %s,\n",
					  convertTSFunction(fout, prsinfo->prsstart));
	appendPQExpBuffer(q, "    GETTOKEN = %s,\n",
					  convertTSFunction(fout, prsinfo->prstoken));
	appendPQExpBuffer(q, "    END = %s,\n",
					  convertTSFunction(fout, prsinfo->prsend));
	if (prsinfo->prsheadline != InvalidOid)
		appendPQExpBuffer(q, "    HEADLINE = %s,\n",
						  convertTSFunction(fout, prsinfo->prsheadline));
	appendPQExpBuffer(q, "    LEXTYPES = %s );\n",
					  convertTSFunction(fout, prsinfo->prslextype));

	appendPQExpBuffer(delq, "DROP TEXT SEARCH PARSER %s;\n",
					  fmtQualifiedDumpable(prsinfo));

	

	if (prsinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, prsinfo->dobj.catId, prsinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = prsinfo->dobj.name,
								  .namespace = prsinfo->dobj.namespace->dobj.name,
								  .description = "TEXT SEARCH PARSER",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Parser Comments */
	if (prsinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "TEXT SEARCH PARSER", qprsname,
					prsinfo->dobj.namespace->dobj.name, "",
					prsinfo->dobj.catId, 0, prsinfo->dobj.dumpId);

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	free(qprsname);
}

/*
 * dumpTSDictionary
 *	  write out a single text search dictionary
 */
static void
dumpTSDictionary(Archive *fout, const TSDictInfo *dictinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PQExpBuffer delq;
	PQExpBuffer query;
	char	   *qdictname;
	PGresult   *res;
	char	   *nspname;
	char	   *tmplname;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();
	query = createPQExpBuffer();

	qdictname = pg_strdup(fmtId(dictinfo->dobj.name));

	/* Fetch name and namespace of the dictionary's template */
	appendPQExpBuffer(query, "SELECT nspname, tmplname "
					  "FROM pg_ts_template p, pg_namespace n "
					  "WHERE p.oid = '%u' AND n.oid = tmplnamespace",
					  dictinfo->dicttemplate);
	res = ExecuteSqlQueryForSingleRow(fout, query->data);
	nspname = PQgetvalue(res, 0, 0);
	tmplname = PQgetvalue(res, 0, 1);

	appendPQExpBuffer(q, "CREATE TEXT SEARCH DICTIONARY %s (\n",
					  fmtQualifiedDumpable(dictinfo));

	appendPQExpBufferStr(q, "    TEMPLATE = ");
	appendPQExpBuffer(q, "%s.", fmtId(nspname));
	appendPQExpBufferStr(q, fmtId(tmplname));

	PQclear(res);

	/* the dictinitoption can be dumped straight into the command */
	if (dictinfo->dictinitoption)
		appendPQExpBuffer(q, ",\n    %s", dictinfo->dictinitoption);

	appendPQExpBufferStr(q, " );\n");

	appendPQExpBuffer(delq, "DROP TEXT SEARCH DICTIONARY %s;\n",
					  fmtQualifiedDumpable(dictinfo));

	

	if (dictinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, dictinfo->dobj.catId, dictinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = dictinfo->dobj.name,
								  .namespace = dictinfo->dobj.namespace->dobj.name,
								  .owner = dictinfo->rolname,
								  .description = "TEXT SEARCH DICTIONARY",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Dictionary Comments */
	if (dictinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "TEXT SEARCH DICTIONARY", qdictname,
					dictinfo->dobj.namespace->dobj.name, dictinfo->rolname,
					dictinfo->dobj.catId, 0, dictinfo->dobj.dumpId);

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(query);
	free(qdictname);
}

/*
 * dumpTSTemplate
 *	  write out a single text search template
 */
static void
dumpTSTemplate(Archive *fout, const TSTemplateInfo *tmplinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PQExpBuffer delq;
	char	   *qtmplname;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();

	qtmplname = pg_strdup(fmtId(tmplinfo->dobj.name));

	appendPQExpBuffer(q, "CREATE TEXT SEARCH TEMPLATE %s (\n",
					  fmtQualifiedDumpable(tmplinfo));

	if (tmplinfo->tmplinit != InvalidOid)
		appendPQExpBuffer(q, "    INIT = %s,\n",
						  convertTSFunction(fout, tmplinfo->tmplinit));
	appendPQExpBuffer(q, "    LEXIZE = %s );\n",
					  convertTSFunction(fout, tmplinfo->tmpllexize));

	appendPQExpBuffer(delq, "DROP TEXT SEARCH TEMPLATE %s;\n",
					  fmtQualifiedDumpable(tmplinfo));

	
	if (tmplinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, tmplinfo->dobj.catId, tmplinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tmplinfo->dobj.name,
								  .namespace = tmplinfo->dobj.namespace->dobj.name,
								  .description = "TEXT SEARCH TEMPLATE",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Template Comments */
	if (tmplinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "TEXT SEARCH TEMPLATE", qtmplname,
					tmplinfo->dobj.namespace->dobj.name, "",
					tmplinfo->dobj.catId, 0, tmplinfo->dobj.dumpId);

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	free(qtmplname);
}

/*
 * dumpTSConfig
 *	  write out a single text search configuration
 */
static void
dumpTSConfig(Archive *fout, const TSConfigInfo *cfginfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PQExpBuffer delq;
	PQExpBuffer query;
	char	   *qcfgname;
	PGresult   *res;
	char	   *nspname;
	char	   *prsname;
	int			ntups,
				i;
	int			i_tokenname;
	int			i_dictname;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();
	query = createPQExpBuffer();

	qcfgname = pg_strdup(fmtId(cfginfo->dobj.name));

	/* Fetch name and namespace of the config's parser */
	appendPQExpBuffer(query, "SELECT nspname, prsname "
					  "FROM pg_ts_parser p, pg_namespace n "
					  "WHERE p.oid = '%u' AND n.oid = prsnamespace",
					  cfginfo->cfgparser);
	res = ExecuteSqlQueryForSingleRow(fout, query->data);
	nspname = PQgetvalue(res, 0, 0);
	prsname = PQgetvalue(res, 0, 1);

	appendPQExpBuffer(q, "CREATE TEXT SEARCH CONFIGURATION %s (\n",
					  fmtQualifiedDumpable(cfginfo));

	appendPQExpBuffer(q, "    PARSER = %s.", fmtId(nspname));
	appendPQExpBuffer(q, "%s );\n", fmtId(prsname));

	PQclear(res);

	resetPQExpBuffer(query);
	appendPQExpBuffer(query,
					  "SELECT\n"
					  "  ( SELECT alias FROM pg_catalog.ts_token_type('%u'::pg_catalog.oid) AS t\n"
					  "    WHERE t.tokid = m.maptokentype ) AS tokenname,\n"
					  "  m.mapdict::pg_catalog.regdictionary AS dictname\n"
					  "FROM pg_catalog.pg_ts_config_map AS m\n"
					  "WHERE m.mapcfg = '%u'\n"
					  "ORDER BY m.mapcfg, m.maptokentype, m.mapseqno",
					  cfginfo->cfgparser, cfginfo->dobj.catId.oid);

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);
	ntups = PQntuples(res);

	i_tokenname = PQfnumber(res, "tokenname");
	i_dictname = PQfnumber(res, "dictname");

	for (i = 0; i < ntups; i++)
	{
		char	   *tokenname = PQgetvalue(res, i, i_tokenname);
		char	   *dictname = PQgetvalue(res, i, i_dictname);

		if (i == 0 ||
			strcmp(tokenname, PQgetvalue(res, i - 1, i_tokenname)) != 0)
		{
			/* starting a new token type, so start a new command */
			if (i > 0)
				appendPQExpBufferStr(q, ";\n");
			appendPQExpBuffer(q, "\nALTER TEXT SEARCH CONFIGURATION %s\n",
							  fmtQualifiedDumpable(cfginfo));
			/* tokenname needs quoting, dictname does NOT */
			appendPQExpBuffer(q, "    ADD MAPPING FOR %s WITH %s",
							  fmtId(tokenname), dictname);
		}
		else
			appendPQExpBuffer(q, ", %s", dictname);
	}

	if (ntups > 0)
		appendPQExpBufferStr(q, ";\n");

	PQclear(res);

	appendPQExpBuffer(delq, "DROP TEXT SEARCH CONFIGURATION %s;\n",
					  fmtQualifiedDumpable(cfginfo));

	

	if (cfginfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, cfginfo->dobj.catId, cfginfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = cfginfo->dobj.name,
								  .namespace = cfginfo->dobj.namespace->dobj.name,
								  .owner = cfginfo->rolname,
								  .description = "TEXT SEARCH CONFIGURATION",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Configuration Comments */
	if (cfginfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "TEXT SEARCH CONFIGURATION", qcfgname,
					cfginfo->dobj.namespace->dobj.name, cfginfo->rolname,
					cfginfo->dobj.catId, 0, cfginfo->dobj.dumpId);

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(query);
	free(qcfgname);
}

/*
 * dumpForeignDataWrapper
 *	  write out a single foreign-data wrapper definition
 */
static void
dumpForeignDataWrapper(Archive *fout, const FdwInfo *fdwinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PQExpBuffer delq;
	char	   *qfdwname;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();

	qfdwname = pg_strdup(fmtId(fdwinfo->dobj.name));

	appendPQExpBuffer(q, "CREATE FOREIGN DATA WRAPPER %s",
					  qfdwname);

	if (strcmp(fdwinfo->fdwhandler, "-") != 0)
		appendPQExpBuffer(q, " HANDLER %s", fdwinfo->fdwhandler);

	if (strcmp(fdwinfo->fdwvalidator, "-") != 0)
		appendPQExpBuffer(q, " VALIDATOR %s", fdwinfo->fdwvalidator);

	if (strlen(fdwinfo->fdwoptions) > 0)
		appendPQExpBuffer(q, " OPTIONS (\n    %s\n)", fdwinfo->fdwoptions);

	appendPQExpBufferStr(q, ";\n");

	appendPQExpBuffer(delq, "DROP FOREIGN DATA WRAPPER %s;\n",
					  qfdwname);

	
	if (fdwinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, fdwinfo->dobj.catId, fdwinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = fdwinfo->dobj.name,
								  .owner = fdwinfo->rolname,
								  .description = "FOREIGN DATA WRAPPER",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Foreign Data Wrapper Comments */
	if (fdwinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "FOREIGN DATA WRAPPER", qfdwname,
					NULL, fdwinfo->rolname,
					fdwinfo->dobj.catId, 0, fdwinfo->dobj.dumpId);

	/* Handle the ACL */
	if (fdwinfo->dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, fdwinfo->dobj.dumpId, InvalidDumpId,
				"FOREIGN DATA WRAPPER", qfdwname, NULL, NULL,
				NULL, fdwinfo->rolname, &fdwinfo->dacl);

	free(qfdwname);

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
}

/*
 * dumpForeignServer
 *	  write out a foreign server definition
 */
static void
dumpForeignServer(Archive *fout, const ForeignServerInfo *srvinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PQExpBuffer delq;
	PQExpBuffer query;
	PGresult   *res;
	char	   *qsrvname;
	char	   *fdwname;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();
	query = createPQExpBuffer();

	qsrvname = pg_strdup(fmtId(srvinfo->dobj.name));

	/* look up the foreign-data wrapper */
	appendPQExpBuffer(query, "SELECT fdwname "
					  "FROM pg_foreign_data_wrapper w "
					  "WHERE w.oid = '%u'",
					  srvinfo->srvfdw);
	res = ExecuteSqlQueryForSingleRow(fout, query->data);
	fdwname = PQgetvalue(res, 0, 0);

	appendPQExpBuffer(q, "CREATE SERVER %s", qsrvname);
	if (srvinfo->srvtype && strlen(srvinfo->srvtype) > 0)
	{
		appendPQExpBufferStr(q, " TYPE ");
		appendStringLiteralAH(q, srvinfo->srvtype, fout);
	}
	if (srvinfo->srvversion && strlen(srvinfo->srvversion) > 0)
	{
		appendPQExpBufferStr(q, " VERSION ");
		appendStringLiteralAH(q, srvinfo->srvversion, fout);
	}

	appendPQExpBufferStr(q, " FOREIGN DATA WRAPPER ");
	appendPQExpBufferStr(q, fmtId(fdwname));

	if (srvinfo->srvoptions && strlen(srvinfo->srvoptions) > 0)
		appendPQExpBuffer(q, " OPTIONS (\n    %s\n)", srvinfo->srvoptions);

	appendPQExpBufferStr(q, ";\n");

	appendPQExpBuffer(delq, "DROP SERVER %s;\n",
					  qsrvname);

	

	if (srvinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, srvinfo->dobj.catId, srvinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = srvinfo->dobj.name,
								  .owner = srvinfo->rolname,
								  .description = "SERVER",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Foreign Server Comments */
	if (srvinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "SERVER", qsrvname,
					NULL, srvinfo->rolname,
					srvinfo->dobj.catId, 0, srvinfo->dobj.dumpId);

	/* Handle the ACL */
	if (srvinfo->dobj.dump & DUMP_COMPONENT_ACL)
		dumpACL(fout, srvinfo->dobj.dumpId, InvalidDumpId,
				"FOREIGN SERVER", qsrvname, NULL, NULL,
				NULL, srvinfo->rolname, &srvinfo->dacl);

	/* Dump user mappings */
	if (srvinfo->dobj.dump & DUMP_COMPONENT_USERMAP)
		dumpUserMappings(fout,
						 srvinfo->dobj.name, NULL,
						 srvinfo->rolname,
						 srvinfo->dobj.catId, srvinfo->dobj.dumpId);

	PQclear(res);

	free(qsrvname);

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(query);
}

/*
 * dumpUserMappings
 *
 * This routine is used to dump any user mappings associated with the
 * server handed to this routine. Should be called after ArchiveEntry()
 * for the server.
 */
static void
dumpUserMappings(Archive *fout,
				 const char *servername, const char *namespace,
				 const char *owner,
				 CatalogId catalogId, DumpId dumpId)
{
	PQExpBuffer q;
	PQExpBuffer delq;
	PQExpBuffer query;
	PQExpBuffer tag;
	PGresult   *res;
	int			ntups;
	int			i_usename;
	int			i_umoptions;
	int			i;

	q = createPQExpBuffer();
	tag = createPQExpBuffer();
	delq = createPQExpBuffer();
	query = createPQExpBuffer();

	/*
	 * We read from the publicly accessible view pg_user_mappings, so as not
	 * to fail if run by a non-superuser.  Note that the view will show
	 * umoptions as null if the user hasn't got privileges for the associated
	 * server; this means that pg_dump will dump such a mapping, but with no
	 * OPTIONS clause.  A possible alternative is to skip such mappings
	 * altogether, but it's not clear that that's an improvement.
	 */
	appendPQExpBuffer(query,
					  "SELECT usename, "
					  "array_to_string(ARRAY("
					  "SELECT quote_ident(option_name) || ' ' || "
					  "quote_literal(option_value) "
					  "FROM pg_options_to_table(umoptions) "
					  "ORDER BY option_name"
					  "), E',\n    ') AS umoptions "
					  "FROM pg_user_mappings "
					  "WHERE srvid = '%u' "
					  "ORDER BY usename",
					  catalogId.oid);

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	ntups = PQntuples(res);
	i_usename = PQfnumber(res, "usename");
	i_umoptions = PQfnumber(res, "umoptions");

	for (i = 0; i < ntups; i++)
	{
		char	   *usename;
		char	   *umoptions;

		usename = PQgetvalue(res, i, i_usename);
		umoptions = PQgetvalue(res, i, i_umoptions);

		resetPQExpBuffer(q);
		appendPQExpBuffer(q, "CREATE USER MAPPING FOR %s", fmtId(usename));
		appendPQExpBuffer(q, " SERVER %s", fmtId(servername));

		if (umoptions && strlen(umoptions) > 0)
			appendPQExpBuffer(q, " OPTIONS (\n    %s\n)", umoptions);

		appendPQExpBufferStr(q, ";\n");

		resetPQExpBuffer(delq);
		appendPQExpBuffer(delq, "DROP USER MAPPING FOR %s", fmtId(usename));
		appendPQExpBuffer(delq, " SERVER %s;\n", fmtId(servername));

		resetPQExpBuffer(tag);
		appendPQExpBuffer(tag, "USER MAPPING %s SERVER %s",
						  usename, servername);

		ArchiveEntry(fout, nilCatalogId, createDumpId(),
					 ARCHIVE_OPTS(.tag = tag->data,
								  .namespace = namespace,
								  .owner = owner,
								  .description = "USER MAPPING",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));
	}

	PQclear(res);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(tag);
	destroyPQExpBuffer(q);
}

/*
 * Write out default privileges information
 */
static void
dumpDefaultACL(Archive *fout, const DefaultACLInfo *daclinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PQExpBuffer tag;
	const char *type;

	/* Do nothing in data-only dump, or if we're skipping ACLs */
	if (dopt->dataOnly || dopt->aclsSkip)
		return;

	q = createPQExpBuffer();
	tag = createPQExpBuffer();

	switch (daclinfo->defaclobjtype)
	{
		case DEFACLOBJ_RELATION:
			type = "TABLES";
			break;
		case DEFACLOBJ_SEQUENCE:
			type = "SEQUENCES";
			break;
		case DEFACLOBJ_FUNCTION:
			type = "FUNCTIONS";
			break;
		case DEFACLOBJ_TYPE:
			type = "TYPES";
			break;
		case DEFACLOBJ_NAMESPACE:
			type = "SCHEMAS";
			break;
		default:
			/* shouldn't get here */
			pg_fatal("unrecognized object type in default privileges: %d",
					 (int) daclinfo->defaclobjtype);
			type = "";			/* keep compiler quiet */
	}

	appendPQExpBuffer(tag, "DEFAULT PRIVILEGES FOR %s", type);

	/* build the actual command(s) for this tuple */
	if (!buildDefaultACLCommands(type,
								 daclinfo->dobj.namespace != NULL ?
								 daclinfo->dobj.namespace->dobj.name : NULL,
								 daclinfo->dacl.acl,
								 daclinfo->dacl.acldefault,
								 daclinfo->defaclrole,
								 fout->remoteVersion,
								 q))
		pg_fatal("could not parse default ACL list (%s)",
				 daclinfo->dacl.acl);

	if (daclinfo->dobj.dump & DUMP_COMPONENT_ACL)
		ArchiveEntry(fout, daclinfo->dobj.catId, daclinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tag->data,
								  .namespace = daclinfo->dobj.namespace ?
								  daclinfo->dobj.namespace->dobj.name : NULL,
								  .owner = daclinfo->defaclrole,
								  .description = "DEFAULT ACL",
								  .section = SECTION_POST_DATA,
								  .createStmt = q->data));

	destroyPQExpBuffer(tag);
	destroyPQExpBuffer(q);
}

/*----------
 * Write out grant/revoke information
 *
 * 'objDumpId' is the dump ID of the underlying object.
 * 'altDumpId' can be a second dumpId that the ACL entry must also depend on,
 *		or InvalidDumpId if there is no need for a second dependency.
 * 'type' must be one of
 *		TABLE, SEQUENCE, FUNCTION, LANGUAGE, SCHEMA, DATABASE, TABLESPACE,
 *		FOREIGN DATA WRAPPER, SERVER, or LARGE OBJECT.
 * 'name' is the formatted name of the object.  Must be quoted etc. already.
 * 'subname' is the formatted name of the sub-object, if any.  Must be quoted.
 *		(Currently we assume that subname is only provided for table columns.)
 * 'nspname' is the namespace the object is in (NULL if none).
 * 'tag' is the tag to use for the ACL TOC entry; typically, this is NULL
 *		to use the default for the object type.
 * 'owner' is the owner, NULL if there is no owner (for languages).
 * 'dacl' is the DumpableAcl struct for the object.
 *
 * Returns the dump ID assigned to the ACL TocEntry, or InvalidDumpId if
 * no ACL entry was created.
 *----------
 */
static DumpId
dumpACL(Archive *fout, DumpId objDumpId, DumpId altDumpId,
		const char *type, const char *name, const char *subname,
		const char *nspname, const char *tag, const char *owner,
		const DumpableAcl *dacl)
{
	DumpId		aclDumpId = InvalidDumpId;
	DumpOptions *dopt = fout->dopt;
	const char *acls = dacl->acl;
	const char *acldefault = dacl->acldefault;
	char		privtype = dacl->privtype;
	const char *initprivs = dacl->initprivs;
	const char *baseacls;
	PQExpBuffer sql;

	/* Do nothing if ACL dump is not enabled */
	if (dopt->aclsSkip)
		return InvalidDumpId;

	/* --data-only skips ACLs *except* large object ACLs */
	if (dopt->dataOnly && strcmp(type, "LARGE OBJECT") != 0)
		return InvalidDumpId;

	sql = createPQExpBuffer();

	/*
	 * In binary upgrade mode, we don't run an extension's script but instead
	 * dump out the objects independently and then recreate them.  To preserve
	 * any initial privileges which were set on extension objects, we need to
	 * compute the set of GRANT and REVOKE commands necessary to get from the
	 * default privileges of an object to its initial privileges as recorded
	 * in pg_init_privs.
	 *
	 * At restore time, we apply these commands after having called
	 * binary_upgrade_set_record_init_privs(true).  That tells the backend to
	 * copy the results into pg_init_privs.  This is how we preserve the
	 * contents of that catalog across binary upgrades.
	 */
	if (dopt->binary_upgrade && privtype == 'e' &&
		initprivs && *initprivs != '\0')
	{
		appendPQExpBufferStr(sql, "SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);\n");
		if (!buildACLCommands(name, subname, nspname, type,
							  initprivs, acldefault, owner,
							  "", fout->remoteVersion, sql))
			pg_fatal("could not parse initial ACL list (%s) or default (%s) for object \"%s\" (%s)",
					 initprivs, acldefault, name, type);
		appendPQExpBufferStr(sql, "SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);\n");
	}

	/*
	 * Now figure the GRANT and REVOKE commands needed to get to the object's
	 * actual current ACL, starting from the initprivs if given, else from the
	 * object-type-specific default.  Also, while buildACLCommands will assume
	 * that a NULL/empty acls string means it needn't do anything, what that
	 * actually represents is the object-type-specific default; so we need to
	 * substitute the acldefault string to get the right results in that case.
	 */
	if (initprivs && *initprivs != '\0')
	{
		baseacls = initprivs;
		if (acls == NULL || *acls == '\0')
			acls = acldefault;
	}
	else
		baseacls = acldefault;

	if (!buildACLCommands(name, subname, nspname, type,
						  acls, baseacls, owner,
						  "", fout->remoteVersion, sql))
		pg_fatal("could not parse ACL list (%s) or default (%s) for object \"%s\" (%s)",
				 acls, baseacls, name, type);

	if (sql->len > 0)
	{
		PQExpBuffer tagbuf = createPQExpBuffer();
		DumpId		aclDeps[2];
		int			nDeps = 0;

		if (tag)
			appendPQExpBufferStr(tagbuf, tag);
		else if (subname)
			appendPQExpBuffer(tagbuf, "COLUMN %s.%s", name, subname);
		else
			appendPQExpBuffer(tagbuf, "%s %s", type, name);

		aclDeps[nDeps++] = objDumpId;
		if (altDumpId != InvalidDumpId)
			aclDeps[nDeps++] = altDumpId;

		aclDumpId = createDumpId();

		ArchiveEntry(fout, nilCatalogId, aclDumpId,
					 ARCHIVE_OPTS(.tag = tagbuf->data,
								  .namespace = nspname,
								  .owner = owner,
								  .description = "ACL",
								  .section = SECTION_NONE,
								  .createStmt = sql->data,
								  .deps = aclDeps,
								  .nDeps = nDeps));

		destroyPQExpBuffer(tagbuf);
	}

	destroyPQExpBuffer(sql);

	return aclDumpId;
}

static int
findSecLabels(Oid classoid, Oid objoid, SecLabelItem **items)
{
	SecLabelItem *middle = NULL;
	SecLabelItem *low;
	SecLabelItem *high;
	int			nmatch;

	if (nseclabels <= 0)		/* no labels, so no match is possible */
	{
		*items = NULL;
		return 0;
	}

	/*
	 * Do binary search to find some item matching the object.
	 */
	low = &seclabels[0];
	high = &seclabels[nseclabels - 1];
	while (low <= high)
	{
		middle = low + (high - low) / 2;

		if (classoid < middle->classoid)
			high = middle - 1;
		else if (classoid > middle->classoid)
			low = middle + 1;
		else if (objoid < middle->objoid)
			high = middle - 1;
		else if (objoid > middle->objoid)
			low = middle + 1;
		else
			break;				/* found a match */
	}

	if (low > high)				/* no matches */
	{
		*items = NULL;
		return 0;
	}

	/*
	 * Now determine how many items match the object.  The search loop
	 * invariant still holds: only items between low and high inclusive could
	 * match.
	 */
	nmatch = 1;
	while (middle > low)
	{
		if (classoid != middle[-1].classoid ||
			objoid != middle[-1].objoid)
			break;
		middle--;
		nmatch++;
	}

	*items = middle;

	middle += nmatch;
	while (middle <= high)
	{
		if (classoid != middle->classoid ||
			objoid != middle->objoid)
			break;
		middle++;
		nmatch++;
	}

	return nmatch;
}

/*
 * dumpSecLabel
 *
 * This routine is used to dump any security labels associated with the
 * object handed to this routine. The routine takes the object type
 * and object name (ready to print, except for schema decoration), plus
 * the namespace and owner of the object (for labeling the ArchiveEntry),
 * plus catalog ID and subid which are the lookup key for pg_seclabel,
 * plus the dump ID for the object (for setting a dependency).
 * If a matching pg_seclabel entry is found, it is dumped.
 *
 * Note: although this routine takes a dumpId for dependency purposes,
 * that purpose is just to mark the dependency in the emitted dump file
 * for possible future use by pg_restore.  We do NOT use it for determining
 * ordering of the label in the dump file, because this routine is called
 * after dependency sorting occurs.  This routine should be called just after
 * calling ArchiveEntry() for the specified object.
 */

static void
dumpTableComment(Archive *fout, const TableInfo *tbinfo,
				 const char *reltypename)
{
	DumpOptions *dopt = fout->dopt;
	CommentItem *comments;
	int			ncomments;
	PQExpBuffer query;
	PQExpBuffer tag;

	/* do nothing, if --no-comments is supplied */
	if (dopt->no_comments)
		return;

	/* Comments are SCHEMA not data */
	if (dopt->dataOnly)
		return;

	/* Search for comments associated with relation, using table */
	ncomments = findComments(tbinfo->dobj.catId.tableoid,
							 tbinfo->dobj.catId.oid,
							 &comments);

	/* If comments exist, build COMMENT ON statements */
	if (ncomments <= 0)
		return;

	query = createPQExpBuffer();
	tag = createPQExpBuffer();

	while (ncomments > 0)
	{
		const char *descr = comments->descr;
		int			objsubid = comments->objsubid;

		if (objsubid == 0)
		{
			resetPQExpBuffer(tag);
			appendPQExpBuffer(tag, "%s %s", reltypename,
							  fmtId(tbinfo->dobj.name));

			resetPQExpBuffer(query);
			appendPQExpBuffer(query, "COMMENT ON %s %s IS ", reltypename,
							  fmtQualifiedDumpable(tbinfo));
			appendStringLiteralAH(query, descr, fout);
			appendPQExpBufferStr(query, ";\n");

			ArchiveEntry(fout, nilCatalogId, createDumpId(),
						 ARCHIVE_OPTS(.tag = tag->data,
									  .namespace = tbinfo->dobj.namespace->dobj.name,
									  .owner = tbinfo->rolname,
									  .description = "COMMENT",
									  .section = SECTION_NONE,
									  .createStmt = query->data,
									  .deps = &(tbinfo->dobj.dumpId),
									  .nDeps = 1));
		}
		else if (objsubid > 0 && objsubid <= tbinfo->numatts)
		{
			resetPQExpBuffer(tag);
			appendPQExpBuffer(tag, "COLUMN %s.",
							  fmtId(tbinfo->dobj.name));
			appendPQExpBufferStr(tag, fmtId(tbinfo->attnames[objsubid - 1]));

			resetPQExpBuffer(query);
			appendPQExpBuffer(query, "COMMENT ON COLUMN %s.",
							  fmtQualifiedDumpable(tbinfo));
			appendPQExpBuffer(query, "%s IS ",
							  fmtId(tbinfo->attnames[objsubid - 1]));
			appendStringLiteralAH(query, descr, fout);
			appendPQExpBufferStr(query, ";\n");

			ArchiveEntry(fout, nilCatalogId, createDumpId(),
						 ARCHIVE_OPTS(.tag = tag->data,
									  .namespace = tbinfo->dobj.namespace->dobj.name,
									  .owner = tbinfo->rolname,
									  .description = "COMMENT",
									  .section = SECTION_NONE,
									  .createStmt = query->data,
									  .deps = &(tbinfo->dobj.dumpId),
									  .nDeps = 1));
		}

		comments++;
		ncomments--;
	}

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(tag);
}

static void
dumpSecLabel(Archive *fout, const char *type, const char *name,
			 const char *namespace, const char *owner,
			 CatalogId catalogId, int subid, DumpId dumpId)
{
	DumpOptions *dopt = fout->dopt;
	SecLabelItem *labels;
	int			nlabels;
	int			i;
	PQExpBuffer query;

	/* do nothing, if --no-security-labels is supplied */
	if (dopt->no_security_labels)
		return;

	/*
	 * Security labels are schema not data ... except large object labels are
	 * data
	 */
	if (strcmp(type, "LARGE OBJECT") != 0)
	{
		if (dopt->dataOnly)
			return;
	}
	else
	{
		/* We do dump large object security labels in binary-upgrade mode */
		if (dopt->schemaOnly && !dopt->binary_upgrade)
			return;
	}

	/* Search for security labels associated with catalogId, using table */
	nlabels = findSecLabels(catalogId.tableoid, catalogId.oid, &labels);

	query = createPQExpBuffer();

	for (i = 0; i < nlabels; i++)
	{
		/*
		 * Ignore label entries for which the subid doesn't match.
		 */
		if (labels[i].objsubid != subid)
			continue;

		appendPQExpBuffer(query,
						  "SECURITY LABEL FOR %s ON %s ",
						  fmtId(labels[i].provider), type);
		if (namespace && *namespace)
			appendPQExpBuffer(query, "%s.", fmtId(namespace));
		appendPQExpBuffer(query, "%s IS ", name);
		appendStringLiteralAH(query, labels[i].label, fout);
		appendPQExpBufferStr(query, ";\n");
	}

	if (query->len > 0)
	{
		PQExpBuffer tag = createPQExpBuffer();

		appendPQExpBuffer(tag, "%s %s", type, name);
		ArchiveEntry(fout, nilCatalogId, createDumpId(),
					 ARCHIVE_OPTS(.tag = tag->data,
								  .namespace = namespace,
								  .owner = owner,
								  .description = "SECURITY LABEL",
								  .section = SECTION_NONE,
								  .createStmt = query->data,
								  .deps = &dumpId,
								  .nDeps = 1));
		destroyPQExpBuffer(tag);
	}

	destroyPQExpBuffer(query);
}

/*
 * dumpTableSecLabel
 *
 * As above, but dump security label for both the specified table (or view)
 * and its columns.
 */
static void
dumpTableSecLabel(Archive *fout, const TableInfo *tbinfo, const char *reltypename)
{
	DumpOptions *dopt = fout->dopt;
	SecLabelItem *labels;
	int			nlabels;
	int			i;
	PQExpBuffer query;
	PQExpBuffer target;

	/* do nothing, if --no-security-labels is supplied */
	if (dopt->no_security_labels)
		return;

	/* SecLabel are SCHEMA not data */
	if (dopt->dataOnly)
		return;

	/* Search for comments associated with relation, using table */
	nlabels = findSecLabels(tbinfo->dobj.catId.tableoid,
							tbinfo->dobj.catId.oid,
							&labels);

	/* If security labels exist, build SECURITY LABEL statements */
	if (nlabels <= 0)
		return;

	query = createPQExpBuffer();
	target = createPQExpBuffer();

	for (i = 0; i < nlabels; i++)
	{
		const char *colname;
		const char *provider = labels[i].provider;
		const char *label = labels[i].label;
		int			objsubid = labels[i].objsubid;

		resetPQExpBuffer(target);
		if (objsubid == 0)
		{
			appendPQExpBuffer(target, "%s %s", reltypename,
							  fmtQualifiedDumpable(tbinfo));
		}
		else
		{
			colname = getAttrName(objsubid, tbinfo);
			/* first fmtXXX result must be consumed before calling again */
			appendPQExpBuffer(target, "COLUMN %s",
							  fmtQualifiedDumpable(tbinfo));
			appendPQExpBuffer(target, ".%s", fmtId(colname));
		}
		appendPQExpBuffer(query, "SECURITY LABEL FOR %s ON %s IS ",
						  fmtId(provider), target->data);
		appendStringLiteralAH(query, label, fout);
		appendPQExpBufferStr(query, ";\n");
	}
	if (query->len > 0)
	{
		resetPQExpBuffer(target);
		appendPQExpBuffer(target, "%s %s", reltypename,
						  fmtId(tbinfo->dobj.name));
		ArchiveEntry(fout, nilCatalogId, createDumpId(),
					 ARCHIVE_OPTS(.tag = target->data,
								  .namespace = tbinfo->dobj.namespace->dobj.name,
								  .owner = tbinfo->rolname,
								  .description = "SECURITY LABEL",
								  .section = SECTION_NONE,
								  .createStmt = query->data,
								  .deps = &(tbinfo->dobj.dumpId),
								  .nDeps = 1));
	}
	destroyPQExpBuffer(query);
	destroyPQExpBuffer(target);
}

/*
 * findSecLabels
 *
 * Find the security label(s), if any, associated with the given object.
 * All the objsubid values associated with the given classoid/objoid are
 * found with one search.
 */

/*
 * collectSecLabels
 *
 * Construct a table of all security labels available for database objects;
 * also set the has-seclabel component flag for each relevant object.
 *
 * The table is sorted by classoid/objid/objsubid for speed in lookup.
 */
static void
collectSecLabels(Archive *fout)
{
	PGresult   *res;
	PQExpBuffer query;
	int			i_label;
	int			i_provider;
	int			i_classoid;
	int			i_objoid;
	int			i_objsubid;
	int			ntups;
	int			i;
	DumpableObject *dobj;

	query = createPQExpBuffer();

	appendPQExpBufferStr(query,
						 "SELECT label, provider, classoid, objoid, objsubid "
						 "FROM pg_catalog.pg_seclabel "
						 "ORDER BY classoid, objoid, objsubid");

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	/* Construct lookup table containing OIDs in numeric form */
	i_label = PQfnumber(res, "label");
	i_provider = PQfnumber(res, "provider");
	i_classoid = PQfnumber(res, "classoid");
	i_objoid = PQfnumber(res, "objoid");
	i_objsubid = PQfnumber(res, "objsubid");

	ntups = PQntuples(res);

	seclabels = (SecLabelItem *) pg_malloc(ntups * sizeof(SecLabelItem));
	nseclabels = 0;
	dobj = NULL;

	for (i = 0; i < ntups; i++)
	{
		CatalogId	objId;
		int			subid;

		objId.tableoid = atooid(PQgetvalue(res, i, i_classoid));
		objId.oid = atooid(PQgetvalue(res, i, i_objoid));
		subid = atoi(PQgetvalue(res, i, i_objsubid));

		/* We needn't remember labels that don't match any dumpable object */
		if (dobj == NULL ||
			dobj->catId.tableoid != objId.tableoid ||
			dobj->catId.oid != objId.oid)
			dobj = findObjectByCatalogId(objId);
		if (dobj == NULL)
			continue;

		/*
		 * Labels on columns of composite types are linked to the type's
		 * pg_class entry, but we need to set the DUMP_COMPONENT_SECLABEL flag
		 * in the type's own DumpableObject.
		 */
		if (subid != 0 && dobj->objType == DO_TABLE &&
			((TableInfo *) dobj)->relkind == RELKIND_COMPOSITE_TYPE)
		{
			TypeInfo   *cTypeInfo;

			cTypeInfo = findTypeByOid(((TableInfo *) dobj)->reltype);
			if (cTypeInfo)
				cTypeInfo->dobj.components |= DUMP_COMPONENT_SECLABEL;
		}
		else
			dobj->components |= DUMP_COMPONENT_SECLABEL;

		seclabels[nseclabels].label = pg_strdup(PQgetvalue(res, i, i_label));
		seclabels[nseclabels].provider = pg_strdup(PQgetvalue(res, i, i_provider));
		seclabels[nseclabels].classoid = objId.tableoid;
		seclabels[nseclabels].objoid = objId.oid;
		seclabels[nseclabels].objsubid = subid;
		nseclabels++;
	}

	PQclear(res);
	destroyPQExpBuffer(query);
}

/*
 * dumpTable
 *	  write out to fout the declarations (not data) of a user-defined table
 */
static void
dumpTable(Archive *fout, const TableInfo *tbinfo)
{
	DumpOptions *dopt = fout->dopt;
	DumpId		tableAclDumpId = InvalidDumpId;
	char	   *namecopy;
	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	if (tbinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
	{
		if (tbinfo->relkind == RELKIND_SEQUENCE) {
			//dumpSequence(fout, tbinfo);
		}
		else
			dumpTableSchema(fout, tbinfo);
	}

	/* Handle the ACL here */
	namecopy = pg_strdup(fmtId(tbinfo->dobj.name));
	if (tbinfo->dobj.dump & DUMP_COMPONENT_ACL)
	{
		const char *objtype =
			(tbinfo->relkind == RELKIND_SEQUENCE) ? "SEQUENCE" : "TABLE";

		tableAclDumpId =
			dumpACL(fout, tbinfo->dobj.dumpId, InvalidDumpId,
					objtype, namecopy, NULL,
					tbinfo->dobj.namespace->dobj.name,
					NULL, tbinfo->rolname, &tbinfo->dacl);
	}

	/*
	 * Handle column ACLs, if any.  Note: we pull these with a separate query
	 * rather than trying to fetch them during getTableAttrs, so that we won't
	 * miss ACLs on system columns.  Doing it this way also allows us to dump
	 * ACLs for catalogs that we didn't mark "interesting" back in getTables.
	 */
	if ((tbinfo->dobj.dump & DUMP_COMPONENT_ACL) && tbinfo->hascolumnACLs)
	{
		PQExpBuffer query = createPQExpBuffer();
		PGresult   *res;
		int			i;

		if (!fout->is_prepared[PREPQUERY_GETCOLUMNACLS])
		{
			/* Set up query for column ACLs */
			appendPQExpBufferStr(query,
								 "PREPARE getColumnACLs(pg_catalog.oid) AS\n");

			if (fout->remoteVersion >= 90600)
			{
				/*
				 * In principle we should call acldefault('c', relowner) to
				 * get the default ACL for a column.  However, we don't
				 * currently store the numeric OID of the relowner in
				 * TableInfo.  We could convert the owner name using regrole,
				 * but that creates a risk of failure due to concurrent role
				 * renames.  Given that the default ACL for columns is empty
				 * and is likely to stay that way, it's not worth extra cycles
				 * and risk to avoid hard-wiring that knowledge here.
				 */
				appendPQExpBufferStr(query,
									 "SELECT at.attname, "
									 "at.attacl, "
									 "'{}' AS acldefault, "
									 "pip.privtype, pip.initprivs "
									 "FROM pg_catalog.pg_attribute at "
									 "LEFT JOIN pg_catalog.pg_init_privs pip ON "
									 "(at.attrelid = pip.objoid "
									 "AND pip.classoid = 'pg_catalog.pg_class'::pg_catalog.regclass "
									 "AND at.attnum = pip.objsubid) "
									 "WHERE at.attrelid = $1 AND "
									 "NOT at.attisdropped "
									 "AND (at.attacl IS NOT NULL OR pip.initprivs IS NOT NULL) "
									 "ORDER BY at.attnum");
			}
			else
			{
				appendPQExpBufferStr(query,
									 "SELECT attname, attacl, '{}' AS acldefault, "
									 "NULL AS privtype, NULL AS initprivs "
									 "FROM pg_catalog.pg_attribute "
									 "WHERE attrelid = $1 AND NOT attisdropped "
									 "AND attacl IS NOT NULL "
									 "ORDER BY attnum");
			}

			ExecuteSqlStatement(fout, query->data);

			fout->is_prepared[PREPQUERY_GETCOLUMNACLS] = true;
		}

		printfPQExpBuffer(query,
						  "EXECUTE getColumnACLs('%u')",
						  tbinfo->dobj.catId.oid);

		res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

		for (i = 0; i < PQntuples(res); i++)
		{
			char	   *attname = PQgetvalue(res, i, 0);
			char	   *attacl = PQgetvalue(res, i, 1);
			char	   *acldefault = PQgetvalue(res, i, 2);
			char		privtype = *(PQgetvalue(res, i, 3));
			char	   *initprivs = PQgetvalue(res, i, 4);
			DumpableAcl coldacl;
			char	   *attnamecopy;

			coldacl.acl = attacl;
			coldacl.acldefault = acldefault;
			coldacl.privtype = privtype;
			coldacl.initprivs = initprivs;
			attnamecopy = pg_strdup(fmtId(attname));

			/*
			 * Column's GRANT type is always TABLE.  Each column ACL depends
			 * on the table-level ACL, since we can restore column ACLs in
			 * parallel but the table-level ACL has to be done first.
			 */
			dumpACL(fout, tbinfo->dobj.dumpId, tableAclDumpId,
					"TABLE", namecopy, attnamecopy,
					tbinfo->dobj.namespace->dobj.name,
					NULL, tbinfo->rolname, &coldacl);
			free(attnamecopy);
		}
		PQclear(res);
		destroyPQExpBuffer(query);
	}

	free(namecopy);
}

/*
 * Create the AS clause for a view or materialized view. The semicolon is
 * stripped because a materialized view must add a WITH NO DATA clause.
 *
 * This returns a new buffer which must be freed by the caller.
 */
static PQExpBuffer
createViewAsClause(Archive *fout, const TableInfo *tbinfo)
{
	PQExpBuffer query = createPQExpBuffer();
	PQExpBuffer result = createPQExpBuffer();
	PGresult   *res;
	int			len;

	/* Fetch the view definition */
	appendPQExpBuffer(query,
					  "SELECT pg_catalog.pg_get_viewdef('%u'::pg_catalog.oid) AS viewdef",
					  tbinfo->dobj.catId.oid);

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	if (PQntuples(res) != 1)
	{
		if (PQntuples(res) < 1)
			pg_fatal("query to obtain definition of view \"%s\" returned no data",
					 tbinfo->dobj.name);
		else
			pg_fatal("query to obtain definition of view \"%s\" returned more than one definition",
					 tbinfo->dobj.name);
	}

	len = PQgetlength(res, 0, 0);

	if (len == 0)
		pg_fatal("definition of view \"%s\" appears to be empty (length zero)",
				 tbinfo->dobj.name);

	/* Strip off the trailing semicolon so that other things may follow. */
	Assert(PQgetvalue(res, 0, 0)[len - 1] == ';');
	appendBinaryPQExpBuffer(result, PQgetvalue(res, 0, 0), len - 1);

	PQclear(res);
	destroyPQExpBuffer(query);

	return result;
}

/*
 * Create a dummy AS clause for a view.  This is used when the real view
 * definition has to be postponed because of circular dependencies.
 * We must duplicate the view's external properties -- column names and types
 * (including collation) -- so that it works for subsequent references.
 *
 * This returns a new buffer which must be freed by the caller.
 */
static PQExpBuffer
createDummyViewAsClause(Archive *fout, const TableInfo *tbinfo)
{
	PQExpBuffer result = createPQExpBuffer();
	int			j;

	appendPQExpBufferStr(result, "SELECT");

	for (j = 0; j < tbinfo->numatts; j++)
	{
		if (j > 0)
			appendPQExpBufferChar(result, ',');
		appendPQExpBufferStr(result, "\n    ");

		appendPQExpBuffer(result, "NULL::%s", tbinfo->atttypnames[j]);

		/*
		 * Must add collation if not default for the type, because CREATE OR
		 * REPLACE VIEW won't change it
		 */
		if (OidIsValid(tbinfo->attcollation[j]))
		{
			CollInfo   *coll;

			coll = findCollationByOid(tbinfo->attcollation[j]);
			if (coll)
				appendPQExpBuffer(result, " COLLATE %s",
								  fmtQualifiedDumpable(coll));
		}

		appendPQExpBuffer(result, " AS %s", fmtId(tbinfo->attnames[j]));
	}

	return result;
}

/*
 * dumpTableSchema
 *	  write the declaration (not data) of one user-defined table or view
 */
static void
dumpTableSchema(Archive *fout, const TableInfo *tbinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q = createPQExpBuffer();
	PQExpBuffer delq = createPQExpBuffer();
	PQExpBuffer extra = createPQExpBuffer();
	char	   *qrelname;
	char	   *qualrelname;
	int			numParents;
	TableInfo **parents;
	int			actual_atts;	/* number of attrs in this CREATE statement */
	const char *reltypename;
	char	   *storage;
	int			j,
				k;

	/* We had better have loaded per-column details about this table */
	Assert(tbinfo->interesting);

	qrelname = pg_strdup(fmtId(tbinfo->dobj.name));
	qualrelname = pg_strdup(fmtQualifiedDumpable(tbinfo));

	if (tbinfo->hasoids)
		pg_log_warning("WITH OIDS is not supported anymore (table \"%s\")",
					   qrelname);

	

	/* Is it a table or a view? */
	if (tbinfo->relkind == RELKIND_VIEW)
	{
		PQExpBuffer result;

		/*
		 * Note: keep this code in sync with the is_view case in dumpRule()
		 */

		reltypename = "VIEW";

		appendPQExpBuffer(delq, "DROP VIEW %s;\n", qualrelname);

		
		appendPQExpBuffer(q, "CREATE VIEW %s", qualrelname);

		if (tbinfo->dummy_view)
			result = createDummyViewAsClause(fout, tbinfo);
		else
		{
			if (nonemptyReloptions(tbinfo->reloptions))
			{
				appendPQExpBufferStr(q, " WITH (");
				appendReloptionsArrayAH(q, tbinfo->reloptions, "", fout);
				appendPQExpBufferChar(q, ')');
			}
			result = createViewAsClause(fout, tbinfo);
		}
		appendPQExpBuffer(q, " AS\n%s", result->data);
		destroyPQExpBuffer(result);

		if (tbinfo->checkoption != NULL && !tbinfo->dummy_view)
			appendPQExpBuffer(q, "\n  WITH %s CHECK OPTION", tbinfo->checkoption);
		appendPQExpBufferStr(q, ";\n");
	}
	else
	{
		char	   *partkeydef = NULL;
		char	   *ftoptions = NULL;
		char	   *srvname = NULL;
		const char *foreign = "";

		/*
		 * Set reltypename, and collect any relkind-specific data that we
		 * didn't fetch during getTables().
		 */
		switch (tbinfo->relkind)
		{
			case RELKIND_PARTITIONED_TABLE:
				{
					PQExpBuffer query = createPQExpBuffer();
					PGresult   *res;

					reltypename = "TABLE";

					/* retrieve partition key definition */
					appendPQExpBuffer(query,
									  "SELECT pg_get_partkeydef('%u')",
									  tbinfo->dobj.catId.oid);
					res = ExecuteSqlQueryForSingleRow(fout, query->data);
					partkeydef = pg_strdup(PQgetvalue(res, 0, 0));
					PQclear(res);
					destroyPQExpBuffer(query);
					break;
				}
			case RELKIND_FOREIGN_TABLE:
				{
					PQExpBuffer query = createPQExpBuffer();
					PGresult   *res;
					int			i_srvname;
					int			i_ftoptions;

					reltypename = "FOREIGN TABLE";

					/* retrieve name of foreign server and generic options */
					appendPQExpBuffer(query,
									  "SELECT fs.srvname, "
									  "pg_catalog.array_to_string(ARRAY("
									  "SELECT pg_catalog.quote_ident(option_name) || "
									  "' ' || pg_catalog.quote_literal(option_value) "
									  "FROM pg_catalog.pg_options_to_table(ftoptions) "
									  "ORDER BY option_name"
									  "), E',\n    ') AS ftoptions "
									  "FROM pg_catalog.pg_foreign_table ft "
									  "JOIN pg_catalog.pg_foreign_server fs "
									  "ON (fs.oid = ft.ftserver) "
									  "WHERE ft.ftrelid = '%u'",
									  tbinfo->dobj.catId.oid);
					res = ExecuteSqlQueryForSingleRow(fout, query->data);
					i_srvname = PQfnumber(res, "srvname");
					i_ftoptions = PQfnumber(res, "ftoptions");
					srvname = pg_strdup(PQgetvalue(res, 0, i_srvname));
					ftoptions = pg_strdup(PQgetvalue(res, 0, i_ftoptions));
					PQclear(res);
					destroyPQExpBuffer(query);

					foreign = "FOREIGN ";
					break;
				}
			case RELKIND_MATVIEW:
				reltypename = "MATERIALIZED VIEW";
				break;
			default:
				reltypename = "TABLE";
				break;
		}

		numParents = tbinfo->numParents;
		parents = tbinfo->parents;

		appendPQExpBuffer(delq, "DROP %s %s;\n", reltypename, qualrelname);

		

		appendPQExpBuffer(q, "CREATE %s%s %s",
						  tbinfo->relpersistence == RELPERSISTENCE_UNLOGGED ?
						  "UNLOGGED " : "",
						  reltypename,
						  qualrelname);

		/*
		 * Attach to type, if reloftype; except in case of a binary upgrade,
		 * we dump the table normally and attach it to the type afterward.
		 */
		if (OidIsValid(tbinfo->reloftype) && !dopt->binary_upgrade)
			appendPQExpBuffer(q, " OF %s",
							  getFormattedTypeName(fout, tbinfo->reloftype,
												   zeroIsError));

		if (tbinfo->relkind != RELKIND_MATVIEW)
		{
			/* Dump the attributes */
			actual_atts = 0;
			for (j = 0; j < tbinfo->numatts; j++)
			{
				/*
				 * Normally, dump if it's locally defined in this table, and
				 * not dropped.  But for binary upgrade, we'll dump all the
				 * columns, and then fix up the dropped and nonlocal cases
				 * below.
				 */
				if (shouldPrintColumn(dopt, tbinfo, j))
				{
					bool		print_default;
					bool		print_notnull;

					/*
					 * Default value --- suppress if to be printed separately
					 * or not at all.
					 */
					print_default = (tbinfo->attrdefs[j] != NULL &&
									 tbinfo->attrdefs[j]->dobj.dump &&
									 !tbinfo->attrdefs[j]->separate);

					/*
					 * Not Null constraint --- suppress if inherited, except
					 * if partition, or in binary-upgrade case where that
					 * won't work.
					 */
					print_notnull = (tbinfo->notnull[j] &&
									 (!tbinfo->inhNotNull[j] ||
									  tbinfo->ispartition || dopt->binary_upgrade));

					/*
					 * Skip column if fully defined by reloftype, except in
					 * binary upgrade
					 */
					if (OidIsValid(tbinfo->reloftype) &&
						!print_default && !print_notnull &&
						!dopt->binary_upgrade)
						continue;

					/* Format properly if not first attr */
					if (actual_atts == 0)
						appendPQExpBufferStr(q, " (");
					else
						appendPQExpBufferChar(q, ',');
					appendPQExpBufferStr(q, "\n    ");
					actual_atts++;

					/* Attribute name */
					appendPQExpBufferStr(q, fmtId(tbinfo->attnames[j]));

					if (tbinfo->attisdropped[j])
					{
						/*
						 * ALTER TABLE DROP COLUMN clears
						 * pg_attribute.atttypid, so we will not have gotten a
						 * valid type name; insert INTEGER as a stopgap. We'll
						 * clean things up later.
						 */
						appendPQExpBufferStr(q, " INTEGER /* dummy */");
						/* and skip to the next column */
						continue;
					}

					/*
					 * Attribute type; print it except when creating a typed
					 * table ('OF type_name'), but in binary-upgrade mode,
					 * print it in that case too.
					 */
					if (dopt->binary_upgrade || !OidIsValid(tbinfo->reloftype))
					{
						appendPQExpBuffer(q, " %s",
										  tbinfo->atttypnames[j]);
					}

					if (print_default)
					{
						if (tbinfo->attgenerated[j] == ATTRIBUTE_GENERATED_STORED)
							appendPQExpBuffer(q, " GENERATED ALWAYS AS (%s) STORED",
											  tbinfo->attrdefs[j]->adef_expr);
						else
							appendPQExpBuffer(q, " DEFAULT %s",
											  tbinfo->attrdefs[j]->adef_expr);
					}


					if (print_notnull)
						appendPQExpBufferStr(q, " NOT NULL");

					/* Add collation if not default for the type */
					if (OidIsValid(tbinfo->attcollation[j]))
					{
						CollInfo   *coll;

						coll = findCollationByOid(tbinfo->attcollation[j]);
						if (coll)
							appendPQExpBuffer(q, " COLLATE %s",
											  fmtQualifiedDumpable(coll));
					}
				}
			}

			/*
			 * Add non-inherited CHECK constraints, if any.
			 *
			 * For partitions, we need to include check constraints even if
			 * they're not defined locally, because the ALTER TABLE ATTACH
			 * PARTITION that we'll emit later expects the constraint to be
			 * there.  (No need to fix conislocal: ATTACH PARTITION does that)
			 */
			for (j = 0; j < tbinfo->ncheck; j++)
			{
				ConstraintInfo *constr = &(tbinfo->checkexprs[j]);

				if (constr->separate ||
					(!constr->conislocal && !tbinfo->ispartition))
					continue;

				if (actual_atts == 0)
					appendPQExpBufferStr(q, " (\n    ");
				else
					appendPQExpBufferStr(q, ",\n    ");

				appendPQExpBuffer(q, "CONSTRAINT %s ",
								  fmtId(constr->dobj.name));
				appendPQExpBufferStr(q, constr->condef);

				actual_atts++;
			}

			if (actual_atts)
				appendPQExpBufferStr(q, "\n)");
			else if (!(OidIsValid(tbinfo->reloftype) && !dopt->binary_upgrade))
			{
				/*
				 * No attributes? we must have a parenthesized attribute list,
				 * even though empty, when not using the OF TYPE syntax.
				 */
				appendPQExpBufferStr(q, " (\n)");
			}

			/*
			 * Emit the INHERITS clause (not for partitions), except in
			 * binary-upgrade mode.
			 */
			if (numParents > 0 && !tbinfo->ispartition &&
				!dopt->binary_upgrade)
			{
				appendPQExpBufferStr(q, "\nINHERITS (");
				for (k = 0; k < numParents; k++)
				{
					TableInfo  *parentRel = parents[k];

					if (k > 0)
						appendPQExpBufferStr(q, ", ");
					appendPQExpBufferStr(q, fmtQualifiedDumpable(parentRel));
				}
				appendPQExpBufferChar(q, ')');
			}

			if (tbinfo->relkind == RELKIND_PARTITIONED_TABLE)
				appendPQExpBuffer(q, "\nPARTITION BY %s", partkeydef);

			if (tbinfo->relkind == RELKIND_FOREIGN_TABLE)
				appendPQExpBuffer(q, "\nSERVER %s", fmtId(srvname));
		}

		if (nonemptyReloptions(tbinfo->reloptions) ||
			nonemptyReloptions(tbinfo->toast_reloptions))
		{
			bool		addcomma = false;

			appendPQExpBufferStr(q, "\nWITH (");
			if (nonemptyReloptions(tbinfo->reloptions))
			{
				addcomma = true;
				appendReloptionsArrayAH(q, tbinfo->reloptions, "", fout);
			}
			if (nonemptyReloptions(tbinfo->toast_reloptions))
			{
				if (addcomma)
					appendPQExpBufferStr(q, ", ");
				appendReloptionsArrayAH(q, tbinfo->toast_reloptions, "toast.",
										fout);
			}
			appendPQExpBufferChar(q, ')');
		}

		/* Dump generic options if any */
		if (ftoptions && ftoptions[0])
			appendPQExpBuffer(q, "\nOPTIONS (\n    %s\n)", ftoptions);

		/*
		 * For materialized views, create the AS clause just like a view. At
		 * this point, we always mark the view as not populated.
		 */
		if (tbinfo->relkind == RELKIND_MATVIEW)
		{
			PQExpBuffer result;

			result = createViewAsClause(fout, tbinfo);
			appendPQExpBuffer(q, " AS\n%s\n  WITH NO DATA;\n",
							  result->data);
			destroyPQExpBuffer(result);
		}
		else
			appendPQExpBufferStr(q, ";\n");

		/* Materialized views can depend on extensions */
		if (tbinfo->relkind == RELKIND_MATVIEW)
			append_depends_on_extension(fout, q, &tbinfo->dobj,
										"pg_catalog.pg_class",
										"MATERIALIZED VIEW",
										qualrelname);

		/*
		 * in binary upgrade mode, update the catalog with any missing values
		 * that might be present.
		 */
		if (dopt->binary_upgrade)
		{
			for (j = 0; j < tbinfo->numatts; j++)
			{
				if (tbinfo->attmissingval[j][0] != '\0')
				{
					appendPQExpBufferStr(q, "\n-- set missing value.\n");
					appendPQExpBufferStr(q,
										 "SELECT pg_catalog.binary_upgrade_set_missing_value(");
					appendStringLiteralAH(q, qualrelname, fout);
					appendPQExpBufferStr(q, "::pg_catalog.regclass,");
					appendStringLiteralAH(q, tbinfo->attnames[j], fout);
					appendPQExpBufferChar(q, ',');
					appendStringLiteralAH(q, tbinfo->attmissingval[j], fout);
					appendPQExpBufferStr(q, ");\n\n");
				}
			}
		}

		/*
		 * To create binary-compatible heap files, we have to ensure the same
		 * physical column order, including dropped columns, as in the
		 * original.  Therefore, we create dropped columns above and drop them
		 * here, also updating their attlen/attalign values so that the
		 * dropped column can be skipped properly.  (We do not bother with
		 * restoring the original attbyval setting.)  Also, inheritance
		 * relationships are set up by doing ALTER TABLE INHERIT rather than
		 * using an INHERITS clause --- the latter would possibly mess up the
		 * column order.  That also means we have to take care about setting
		 * attislocal correctly, plus fix up any inherited CHECK constraints.
		 * Analogously, we set up typed tables using ALTER TABLE / OF here.
		 *
		 * We process foreign and partitioned tables here, even though they
		 * lack heap storage, because they can participate in inheritance
		 * relationships and we want this stuff to be consistent across the
		 * inheritance tree.  We can exclude indexes, toast tables, sequences
		 * and matviews, even though they have storage, because we don't
		 * support altering or dropping columns in them, nor can they be part
		 * of inheritance trees.
		 */
		if (dopt->binary_upgrade &&
			(tbinfo->relkind == RELKIND_RELATION ||
			 tbinfo->relkind == RELKIND_FOREIGN_TABLE ||
			 tbinfo->relkind == RELKIND_PARTITIONED_TABLE))
		{
			bool		firstitem;

			/*
			 * Drop any dropped columns.  Merge the pg_attribute manipulations
			 * into a single SQL command, so that we don't cause repeated
			 * relcache flushes on the target table.  Otherwise we risk O(N^2)
			 * relcache bloat while dropping N columns.
			 */
			resetPQExpBuffer(extra);
			firstitem = true;
			for (j = 0; j < tbinfo->numatts; j++)
			{
				if (tbinfo->attisdropped[j])
				{
					if (firstitem)
					{
						appendPQExpBufferStr(q, "\n-- For binary upgrade, recreate dropped columns.\n"
											 "UPDATE pg_catalog.pg_attribute\n"
											 "SET attlen = v.dlen, "
											 "attalign = v.dalign, "
											 "attbyval = false\n"
											 "FROM (VALUES ");
						firstitem = false;
					}
					else
						appendPQExpBufferStr(q, ",\n             ");
					appendPQExpBufferChar(q, '(');
					appendStringLiteralAH(q, tbinfo->attnames[j], fout);
					appendPQExpBuffer(q, ", %d, '%c')",
									  tbinfo->attlen[j],
									  tbinfo->attalign[j]);
					/* The ALTER ... DROP COLUMN commands must come after */
					appendPQExpBuffer(extra, "ALTER %sTABLE ONLY %s ",
									  foreign, qualrelname);
					appendPQExpBuffer(extra, "DROP COLUMN %s;\n",
									  fmtId(tbinfo->attnames[j]));
				}
			}
			if (!firstitem)
			{
				appendPQExpBufferStr(q, ") v(dname, dlen, dalign)\n"
									 "WHERE attrelid = ");
				appendStringLiteralAH(q, qualrelname, fout);
				appendPQExpBufferStr(q, "::pg_catalog.regclass\n"
									 "  AND attname = v.dname;\n");
				/* Now we can issue the actual DROP COLUMN commands */
				appendBinaryPQExpBuffer(q, extra->data, extra->len);
			}

			/*
			 * Fix up inherited columns.  As above, do the pg_attribute
			 * manipulations in a single SQL command.
			 */
			firstitem = true;
			for (j = 0; j < tbinfo->numatts; j++)
			{
				if (!tbinfo->attisdropped[j] &&
					!tbinfo->attislocal[j])
				{
					if (firstitem)
					{
						appendPQExpBufferStr(q, "\n-- For binary upgrade, recreate inherited columns.\n");
						appendPQExpBufferStr(q, "UPDATE pg_catalog.pg_attribute\n"
											 "SET attislocal = false\n"
											 "WHERE attrelid = ");
						appendStringLiteralAH(q, qualrelname, fout);
						appendPQExpBufferStr(q, "::pg_catalog.regclass\n"
											 "  AND attname IN (");
						firstitem = false;
					}
					else
						appendPQExpBufferStr(q, ", ");
					appendStringLiteralAH(q, tbinfo->attnames[j], fout);
				}
			}
			if (!firstitem)
				appendPQExpBufferStr(q, ");\n");

			/*
			 * Add inherited CHECK constraints, if any.
			 *
			 * For partitions, they were already dumped, and conislocal
			 * doesn't need fixing.
			 *
			 * As above, issue only one direct manipulation of pg_constraint.
			 * Although it is tempting to merge the ALTER ADD CONSTRAINT
			 * commands into one as well, refrain for now due to concern about
			 * possible backend memory bloat if there are many such
			 * constraints.
			 */
			resetPQExpBuffer(extra);
			firstitem = true;
			for (k = 0; k < tbinfo->ncheck; k++)
			{
				ConstraintInfo *constr = &(tbinfo->checkexprs[k]);

				if (constr->separate || constr->conislocal || tbinfo->ispartition)
					continue;

				if (firstitem)
					appendPQExpBufferStr(q, "\n-- For binary upgrade, set up inherited constraints.\n");
				appendPQExpBuffer(q, "ALTER %sTABLE ONLY %s ADD CONSTRAINT %s %s;\n",
								  foreign, qualrelname,
								  fmtId(constr->dobj.name),
								  constr->condef);
				/* Update pg_constraint after all the ALTER TABLEs */
				if (firstitem)
				{
					appendPQExpBufferStr(extra, "UPDATE pg_catalog.pg_constraint\n"
										 "SET conislocal = false\n"
										 "WHERE contype = 'c' AND conrelid = ");
					appendStringLiteralAH(extra, qualrelname, fout);
					appendPQExpBufferStr(extra, "::pg_catalog.regclass\n");
					appendPQExpBufferStr(extra, "  AND conname IN (");
					firstitem = false;
				}
				else
					appendPQExpBufferStr(extra, ", ");
				appendStringLiteralAH(extra, constr->dobj.name, fout);
			}
			if (!firstitem)
			{
				appendPQExpBufferStr(extra, ");\n");
				appendBinaryPQExpBuffer(q, extra->data, extra->len);
			}

			if (numParents > 0 && !tbinfo->ispartition)
			{
				appendPQExpBufferStr(q, "\n-- For binary upgrade, set up inheritance this way.\n");
				for (k = 0; k < numParents; k++)
				{
					TableInfo  *parentRel = parents[k];

					appendPQExpBuffer(q, "ALTER %sTABLE ONLY %s INHERIT %s;\n", foreign,
									  qualrelname,
									  fmtQualifiedDumpable(parentRel));
				}
			}

			if (OidIsValid(tbinfo->reloftype))
			{
				appendPQExpBufferStr(q, "\n-- For binary upgrade, set up typed tables this way.\n");
				appendPQExpBuffer(q, "ALTER TABLE ONLY %s OF %s;\n",
								  qualrelname,
								  getFormattedTypeName(fout, tbinfo->reloftype,
													   zeroIsError));
			}
		}

		/*
		 * In binary_upgrade mode, arrange to restore the old relfrozenxid and
		 * relminmxid of all vacuumable relations.  (While vacuum.c processes
		 * TOAST tables semi-independently, here we see them only as children
		 * of other relations; so this "if" lacks RELKIND_TOASTVALUE, and the
		 * child toast table is handled below.)
		 */
		if (dopt->binary_upgrade &&
			(tbinfo->relkind == RELKIND_RELATION ||
			 tbinfo->relkind == RELKIND_MATVIEW))
		{
			appendPQExpBufferStr(q, "\n-- For binary upgrade, set heap's relfrozenxid and relminmxid\n");
			appendPQExpBuffer(q, "UPDATE pg_catalog.pg_class\n"
							  "SET relfrozenxid = '%u', relminmxid = '%u'\n"
							  "WHERE oid = ",
							  tbinfo->frozenxid, tbinfo->minmxid);
			appendStringLiteralAH(q, qualrelname, fout);
			appendPQExpBufferStr(q, "::pg_catalog.regclass;\n");

			if (tbinfo->toast_oid)
			{
				/*
				 * The toast table will have the same OID at restore, so we
				 * can safely target it by OID.
				 */
				appendPQExpBufferStr(q, "\n-- For binary upgrade, set toast's relfrozenxid and relminmxid\n");
				appendPQExpBuffer(q, "UPDATE pg_catalog.pg_class\n"
								  "SET relfrozenxid = '%u', relminmxid = '%u'\n"
								  "WHERE oid = '%u';\n",
								  tbinfo->toast_frozenxid,
								  tbinfo->toast_minmxid, tbinfo->toast_oid);
			}
		}

		/*
		 * In binary_upgrade mode, restore matviews' populated status by
		 * poking pg_class directly.  This is pretty ugly, but we can't use
		 * REFRESH MATERIALIZED VIEW since it's possible that some underlying
		 * matview is not populated even though this matview is; in any case,
		 * we want to transfer the matview's heap storage, not run REFRESH.
		 */
		if (dopt->binary_upgrade && tbinfo->relkind == RELKIND_MATVIEW &&
			tbinfo->relispopulated)
		{
			appendPQExpBufferStr(q, "\n-- For binary upgrade, mark materialized view as populated\n");
			appendPQExpBufferStr(q, "UPDATE pg_catalog.pg_class\n"
								 "SET relispopulated = 't'\n"
								 "WHERE oid = ");
			appendStringLiteralAH(q, qualrelname, fout);
			appendPQExpBufferStr(q, "::pg_catalog.regclass;\n");
		}

		/*
		 * Dump additional per-column properties that we can't handle in the
		 * main CREATE TABLE command.
		 */
		for (j = 0; j < tbinfo->numatts; j++)
		{
			/* None of this applies to dropped columns */
			if (tbinfo->attisdropped[j])
				continue;

			/*
			 * If we didn't dump the column definition explicitly above, and
			 * it is not-null and did not inherit that property from a parent,
			 * we have to mark it separately.
			 */
			if (!shouldPrintColumn(dopt, tbinfo, j) &&
				tbinfo->notnull[j] && !tbinfo->inhNotNull[j])
				appendPQExpBuffer(q,
								  "ALTER %sTABLE ONLY %s ALTER COLUMN %s SET NOT NULL;\n",
								  foreign, qualrelname,
								  fmtId(tbinfo->attnames[j]));

			/*
			 * Dump per-column statistics information. We only issue an ALTER
			 * TABLE statement if the attstattarget entry for this column is
			 * not the default value.
			 */
			if (tbinfo->attstattarget[j] >= 0)
				appendPQExpBuffer(q, "ALTER %sTABLE ONLY %s ALTER COLUMN %s SET STATISTICS %d;\n",
								  foreign, qualrelname,
								  fmtId(tbinfo->attnames[j]),
								  tbinfo->attstattarget[j]);

			/*
			 * Dump per-column storage information.  The statement is only
			 * dumped if the storage has been changed from the type's default.
			 */
			if (tbinfo->attstorage[j] != tbinfo->typstorage[j])
			{
				switch (tbinfo->attstorage[j])
				{
					case TYPSTORAGE_PLAIN:
						storage = "PLAIN";
						break;
					case TYPSTORAGE_EXTERNAL:
						storage = "EXTERNAL";
						break;
					case TYPSTORAGE_EXTENDED:
						storage = "EXTENDED";
						break;
					case TYPSTORAGE_MAIN:
						storage = "MAIN";
						break;
					default:
						storage = NULL;
				}

				/*
				 * Only dump the statement if it's a storage type we recognize
				 */
				if (storage != NULL)
					appendPQExpBuffer(q, "ALTER %sTABLE ONLY %s ALTER COLUMN %s SET STORAGE %s;\n",
									  foreign, qualrelname,
									  fmtId(tbinfo->attnames[j]),
									  storage);
			}

			/*
			 * Dump per-column compression, if it's been set.
			 */
			if (!dopt->no_toast_compression)
			{
				const char *cmname;

				switch (tbinfo->attcompression[j])
				{
					case 'p':
						cmname = "pglz";
						break;
					case 'l':
						cmname = "lz4";
						break;
					default:
						cmname = NULL;
						break;
				}

				if (cmname != NULL)
					appendPQExpBuffer(q, "ALTER %sTABLE ONLY %s ALTER COLUMN %s SET COMPRESSION %s;\n",
									  foreign, qualrelname,
									  fmtId(tbinfo->attnames[j]),
									  cmname);
			}

			/*
			 * Dump per-column attributes.
			 */
			if (tbinfo->attoptions[j][0] != '\0')
				appendPQExpBuffer(q, "ALTER %sTABLE ONLY %s ALTER COLUMN %s SET (%s);\n",
								  foreign, qualrelname,
								  fmtId(tbinfo->attnames[j]),
								  tbinfo->attoptions[j]);

			/*
			 * Dump per-column fdw options.
			 */
			if (tbinfo->relkind == RELKIND_FOREIGN_TABLE &&
				tbinfo->attfdwoptions[j][0] != '\0')
				appendPQExpBuffer(q,
								  "ALTER FOREIGN TABLE ONLY %s ALTER COLUMN %s OPTIONS (\n"
								  "    %s\n"
								  ");\n",
								  qualrelname,
								  fmtId(tbinfo->attnames[j]),
								  tbinfo->attfdwoptions[j]);
		}						/* end loop over columns */

		free(partkeydef);
		free(ftoptions);
		free(srvname);
	}

	/*
	 * dump properties we only have ALTER TABLE syntax for
	 */
	if ((tbinfo->relkind == RELKIND_RELATION ||
		 tbinfo->relkind == RELKIND_PARTITIONED_TABLE ||
		 tbinfo->relkind == RELKIND_MATVIEW) &&
		tbinfo->relreplident != REPLICA_IDENTITY_DEFAULT)
	{
		if (tbinfo->relreplident == REPLICA_IDENTITY_INDEX)
		{
			/* nothing to do, will be set when the index is dumped */
		}
		else if (tbinfo->relreplident == REPLICA_IDENTITY_NOTHING)
		{
			appendPQExpBuffer(q, "\nALTER TABLE ONLY %s REPLICA IDENTITY NOTHING;\n",
							  qualrelname);
		}
		else if (tbinfo->relreplident == REPLICA_IDENTITY_FULL)
		{
			appendPQExpBuffer(q, "\nALTER TABLE ONLY %s REPLICA IDENTITY FULL;\n",
							  qualrelname);
		}
	}

	if (tbinfo->forcerowsec)
		appendPQExpBuffer(q, "\nALTER TABLE ONLY %s FORCE ROW LEVEL SECURITY;\n",
						  qualrelname);

	

	if (tbinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
	{
		char	   *tablespace = NULL;
		char	   *tableam = NULL;

		/*
		 * _selectTablespace() relies on tablespace-enabled objects in the
		 * default tablespace to have a tablespace of "" (empty string) versus
		 * non-tablespace-enabled objects to have a tablespace of NULL.
		 * getTables() sets tbinfo->reltablespace to "" for the default
		 * tablespace (not NULL).
		 */
		if (RELKIND_HAS_TABLESPACE(tbinfo->relkind))
			tablespace = tbinfo->reltablespace;

		if (RELKIND_HAS_TABLE_AM(tbinfo->relkind) ||
			tbinfo->relkind == RELKIND_PARTITIONED_TABLE)
			tableam = tbinfo->amname;

		ArchiveEntry(fout, tbinfo->dobj.catId, tbinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tbinfo->dobj.name,
								  .namespace = tbinfo->dobj.namespace->dobj.name,
								  .tablespace = tablespace,
								  .tableam = tableam,
								  .relkind = tbinfo->relkind,
								  .owner = tbinfo->rolname,
								  .description = reltypename,
								  .section = tbinfo->postponed_def ?
								  SECTION_POST_DATA : SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));
	}

	/* Dump Table Comments */
	if (tbinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpTableComment(fout, tbinfo, reltypename);

	/* Dump Table Security Labels */
	if (tbinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpTableSecLabel(fout, tbinfo, reltypename);

	/* Dump comments on inlined table constraints */
	for (j = 0; j < tbinfo->ncheck; j++)
	{
		ConstraintInfo *constr = &(tbinfo->checkexprs[j]);

		if (constr->separate || !constr->conislocal)
			continue;

		if (constr->dobj.dump & DUMP_COMPONENT_COMMENT)
			dumpTableConstraintComment(fout, constr);
	}

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(extra);
	free(qrelname);
	free(qualrelname);
}

/*
 * dumpTableAttach
 *	  write to fout the commands to attach a child partition
 *
 * Child partitions are always made by creating them separately
 * and then using ATTACH PARTITION, rather than using
 * CREATE TABLE ... PARTITION OF.  This is important for preserving
 * any possible discrepancy in column layout, to allow assigning the
 * correct tablespace if different, and so that it's possible to restore
 * a partition without restoring its parent.  (You'll get an error from
 * the ATTACH PARTITION command, but that can be ignored, or skipped
 * using "pg_restore -L" if you prefer.)  The last point motivates
 * treating ATTACH PARTITION as a completely separate ArchiveEntry
 * rather than emitting it within the child partition's ArchiveEntry.
 */
static void
dumpTableAttach(Archive *fout, const TableAttachInfo *attachinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PGresult   *res;
	char	   *partbound;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();

	if (!fout->is_prepared[PREPQUERY_DUMPTABLEATTACH])
	{
		/* Set up query for partbound details */
		appendPQExpBufferStr(q,
							 "PREPARE dumpTableAttach(pg_catalog.oid) AS\n");

		appendPQExpBufferStr(q,
							 "SELECT pg_get_expr(c.relpartbound, c.oid) "
							 "FROM pg_class c "
							 "WHERE c.oid = $1");

		ExecuteSqlStatement(fout, q->data);

		fout->is_prepared[PREPQUERY_DUMPTABLEATTACH] = true;
	}

	printfPQExpBuffer(q,
					  "EXECUTE dumpTableAttach('%u')",
					  attachinfo->partitionTbl->dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, q->data);
	partbound = PQgetvalue(res, 0, 0);

	/* Perform ALTER TABLE on the parent */
	printfPQExpBuffer(q,
					  "ALTER TABLE ONLY %s ",
					  fmtQualifiedDumpable(attachinfo->parentTbl));
	appendPQExpBuffer(q,
					  "ATTACH PARTITION %s %s;\n",
					  fmtQualifiedDumpable(attachinfo->partitionTbl),
					  partbound);

	/*
	 * There is no point in creating a drop query as the drop is done by table
	 * drop.  (If you think to change this, see also _printTocEntry().)
	 * Although this object doesn't really have ownership as such, set the
	 * owner field anyway to ensure that the command is run by the correct
	 * role at restore time.
	 */
	ArchiveEntry(fout, attachinfo->dobj.catId, attachinfo->dobj.dumpId,
				 ARCHIVE_OPTS(.tag = attachinfo->dobj.name,
							  .namespace = attachinfo->dobj.namespace->dobj.name,
							  .owner = attachinfo->partitionTbl->rolname,
							  .description = "TABLE ATTACH",
							  .section = SECTION_PRE_DATA,
							  .createStmt = q->data));

	PQclear(res);
	destroyPQExpBuffer(q);
}

/*
 * dumpAttrDef --- dump an attribute's default-value declaration
 */
static void
dumpAttrDef(Archive *fout, const AttrDefInfo *adinfo)
{
	DumpOptions *dopt = fout->dopt;
	TableInfo  *tbinfo = adinfo->adtable;
	int			adnum = adinfo->adnum;
	PQExpBuffer q;
	PQExpBuffer delq;
	char	   *qualrelname;
	char	   *tag;
	char	   *foreign;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	/* Skip if not "separate"; it was dumped in the table's definition */
	if (!adinfo->separate)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();

	qualrelname = pg_strdup(fmtQualifiedDumpable(tbinfo));

	foreign = tbinfo->relkind == RELKIND_FOREIGN_TABLE ? "FOREIGN " : "";

	appendPQExpBuffer(q,
					  "ALTER %sTABLE ONLY %s ALTER COLUMN %s SET DEFAULT %s;\n",
					  foreign, qualrelname, fmtId(tbinfo->attnames[adnum - 1]),
					  adinfo->adef_expr);

	appendPQExpBuffer(delq, "ALTER %sTABLE %s ALTER COLUMN %s DROP DEFAULT;\n",
					  foreign, qualrelname,
					  fmtId(tbinfo->attnames[adnum - 1]));

	tag = psprintf("%s %s", tbinfo->dobj.name, tbinfo->attnames[adnum - 1]);

	if (adinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, adinfo->dobj.catId, adinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tag,
								  .namespace = tbinfo->dobj.namespace->dobj.name,
								  .owner = tbinfo->rolname,
								  .description = "DEFAULT",
								  .section = SECTION_PRE_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	free(tag);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	free(qualrelname);
}

/*
 * getAttrName: extract the correct name for an attribute
 *
 * The array tblInfo->attnames[] only provides names of user attributes;
 * if a system attribute number is supplied, we have to fake it.
 * We also do a little bit of bounds checking for safety's sake.
 */
static const char *
getAttrName(int attrnum, const TableInfo *tblInfo)
{
	if (attrnum > 0 && attrnum <= tblInfo->numatts)
		return tblInfo->attnames[attrnum - 1];
	switch (attrnum)
	{
		case SelfItemPointerAttributeNumber:
			return "ctid";
		case MinTransactionIdAttributeNumber:
			return "xmin";
		case MinCommandIdAttributeNumber:
			return "cmin";
		case MaxTransactionIdAttributeNumber:
			return "xmax";
		case MaxCommandIdAttributeNumber:
			return "cmax";
		case TableOidAttributeNumber:
			return "tableoid";
	}
	pg_fatal("invalid column number %d for table \"%s\"",
			 attrnum, tblInfo->dobj.name);
	return NULL;				/* keep compiler quiet */
}

/*
 * dumpIndex
 *	  write out to fout a user-defined index
 */
static void
dumpIndex(Archive *fout, const IndxInfo *indxinfo)
{
	DumpOptions *dopt = fout->dopt;
	TableInfo  *tbinfo = indxinfo->indextable;
	bool		is_constraint = (indxinfo->indexconstraint != 0);
	PQExpBuffer q;
	PQExpBuffer delq;
	char	   *qindxname;
	char	   *qqindxname;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();

	qindxname = pg_strdup(fmtId(indxinfo->dobj.name));
	qqindxname = pg_strdup(fmtQualifiedDumpable(indxinfo));

	/*
	 * If there's an associated constraint, don't dump the index per se, but
	 * do dump any comment for it.  (This is safe because dependency ordering
	 * will have ensured the constraint is emitted first.)	Note that the
	 * emitted comment has to be shown as depending on the constraint, not the
	 * index, in such cases.
	 */
	if (!is_constraint)
	{
		char	   *indstatcols = indxinfo->indstatcols;
		char	   *indstatvals = indxinfo->indstatvals;
		char	  **indstatcolsarray = NULL;
		char	  **indstatvalsarray = NULL;
		int			nstatcols = 0;
		int			nstatvals = 0;

		

		/* Plain secondary index */
		appendPQExpBuffer(q, "%s;\n", indxinfo->indexdef);

		/*
		 * Append ALTER TABLE commands as needed to set properties that we
		 * only have ALTER TABLE syntax for.  Keep this in sync with the
		 * similar code in dumpConstraint!
		 */

		/* If the index is clustered, we need to record that. */
		if (indxinfo->indisclustered)
		{
			appendPQExpBuffer(q, "\nALTER TABLE %s CLUSTER",
							  fmtQualifiedDumpable(tbinfo));
			/* index name is not qualified in this syntax */
			appendPQExpBuffer(q, " ON %s;\n",
							  qindxname);
		}

		/*
		 * If the index has any statistics on some of its columns, generate
		 * the associated ALTER INDEX queries.
		 */
		if (strlen(indstatcols) != 0 || strlen(indstatvals) != 0)
		{
			int			j;

			if (!parsePGArray(indstatcols, &indstatcolsarray, &nstatcols))
				pg_fatal("could not parse index statistic columns");
			if (!parsePGArray(indstatvals, &indstatvalsarray, &nstatvals))
				pg_fatal("could not parse index statistic values");
			if (nstatcols != nstatvals)
				pg_fatal("mismatched number of columns and values for index statistics");

			for (j = 0; j < nstatcols; j++)
			{
				appendPQExpBuffer(q, "ALTER INDEX %s ", qqindxname);

				/*
				 * Note that this is a column number, so no quotes should be
				 * used.
				 */
				appendPQExpBuffer(q, "ALTER COLUMN %s ",
								  indstatcolsarray[j]);
				appendPQExpBuffer(q, "SET STATISTICS %s;\n",
								  indstatvalsarray[j]);
			}
		}

		/* Indexes can depend on extensions */
		append_depends_on_extension(fout, q, &indxinfo->dobj,
									"pg_catalog.pg_class",
									"INDEX", qqindxname);

		/* If the index defines identity, we need to record that. */
		if (indxinfo->indisreplident)
		{
			appendPQExpBuffer(q, "\nALTER TABLE ONLY %s REPLICA IDENTITY USING",
							  fmtQualifiedDumpable(tbinfo));
			/* index name is not qualified in this syntax */
			appendPQExpBuffer(q, " INDEX %s;\n",
							  qindxname);
		}

		/*
		 * If this index is a member of a partitioned index, the backend will
		 * not allow us to drop it separately, so don't try.  It will go away
		 * automatically when we drop either the index's table or the
		 * partitioned index.  (If, in a selective restore with --clean, we
		 * drop neither of those, then this index will not be dropped either.
		 * But that's fine, and even if you think it's not, the backend won't
		 * let us do differently.)
		 */
		if (indxinfo->parentidx == 0)
			appendPQExpBuffer(delq, "DROP INDEX %s;\n", qqindxname);

		if (indxinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
			ArchiveEntry(fout, indxinfo->dobj.catId, indxinfo->dobj.dumpId,
						 ARCHIVE_OPTS(.tag = indxinfo->dobj.name,
									  .namespace = tbinfo->dobj.namespace->dobj.name,
									  .tablespace = indxinfo->tablespace,
									  .owner = tbinfo->rolname,
									  .description = "INDEX",
									  .section = SECTION_POST_DATA,
									  .createStmt = q->data,
									  .dropStmt = delq->data));

		free(indstatcolsarray);
		free(indstatvalsarray);
	}

	/* Dump Index Comments */
	if (indxinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "INDEX", qindxname,
					tbinfo->dobj.namespace->dobj.name,
					tbinfo->rolname,
					indxinfo->dobj.catId, 0,
					is_constraint ? indxinfo->indexconstraint :
					indxinfo->dobj.dumpId);

	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	free(qindxname);
	free(qqindxname);
}

/*
 * dumpIndexAttach
 *	  write out to fout a partitioned-index attachment clause
 */
static void
dumpIndexAttach(Archive *fout, const IndexAttachInfo *attachinfo)
{
	/* Do nothing in data-only dump */
	if (fout->dopt->dataOnly)
		return;

	if (attachinfo->partitionIdx->dobj.dump & DUMP_COMPONENT_DEFINITION)
	{
		PQExpBuffer q = createPQExpBuffer();

		appendPQExpBuffer(q, "ALTER INDEX %s ",
						  fmtQualifiedDumpable(attachinfo->parentIdx));
		appendPQExpBuffer(q, "ATTACH PARTITION %s;\n",
						  fmtQualifiedDumpable(attachinfo->partitionIdx));

		/*
		 * There is no need for a dropStmt since the drop is done implicitly
		 * when we drop either the index's table or the partitioned index.
		 * Moreover, since there's no ALTER INDEX DETACH PARTITION command,
		 * there's no way to do it anyway.  (If you think to change this,
		 * consider also what to do with --if-exists.)
		 *
		 * Although this object doesn't really have ownership as such, set the
		 * owner field anyway to ensure that the command is run by the correct
		 * role at restore time.
		 */
		ArchiveEntry(fout, attachinfo->dobj.catId, attachinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = attachinfo->dobj.name,
								  .namespace = attachinfo->dobj.namespace->dobj.name,
								  .owner = attachinfo->parentIdx->indextable->rolname,
								  .description = "INDEX ATTACH",
								  .section = SECTION_POST_DATA,
								  .createStmt = q->data));

		destroyPQExpBuffer(q);
	}
}

/*
 * dumpStatisticsExt
 *	  write out to fout an extended statistics object
 */
static void
dumpStatisticsExt(Archive *fout, const StatsExtInfo *statsextinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer q;
	PQExpBuffer delq;
	PQExpBuffer query;
	char	   *qstatsextname;
	PGresult   *res;
	char	   *stxdef;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();
	query = createPQExpBuffer();

	qstatsextname = pg_strdup(fmtId(statsextinfo->dobj.name));

	appendPQExpBuffer(query, "SELECT "
					  "pg_catalog.pg_get_statisticsobjdef('%u'::pg_catalog.oid)",
					  statsextinfo->dobj.catId.oid);

	res = ExecuteSqlQueryForSingleRow(fout, query->data);

	stxdef = PQgetvalue(res, 0, 0);

	/* Result of pg_get_statisticsobjdef is complete except for semicolon */
	appendPQExpBuffer(q, "%s;\n", stxdef);

	/*
	 * We only issue an ALTER STATISTICS statement if the stxstattarget entry
	 * for this statistics object is not the default value.
	 */
	if (statsextinfo->stattarget >= 0)
	{
		appendPQExpBuffer(q, "ALTER STATISTICS %s ",
						  fmtQualifiedDumpable(statsextinfo));
		appendPQExpBuffer(q, "SET STATISTICS %d;\n",
						  statsextinfo->stattarget);
	}

	appendPQExpBuffer(delq, "DROP STATISTICS %s;\n",
					  fmtQualifiedDumpable(statsextinfo));

	if (statsextinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, statsextinfo->dobj.catId,
					 statsextinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = statsextinfo->dobj.name,
								  .namespace = statsextinfo->dobj.namespace->dobj.name,
								  .owner = statsextinfo->rolname,
								  .description = "STATISTICS",
								  .section = SECTION_POST_DATA,
								  .createStmt = q->data,
								  .dropStmt = delq->data));

	/* Dump Statistics Comments */
	if (statsextinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "STATISTICS", qstatsextname,
					statsextinfo->dobj.namespace->dobj.name,
					statsextinfo->rolname,
					statsextinfo->dobj.catId, 0,
					statsextinfo->dobj.dumpId);

	PQclear(res);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
	destroyPQExpBuffer(query);
	free(qstatsextname);
}

/*
 * dumpConstraint
 *	  write out to fout a user-defined constraint
 */
static void
dumpConstraint(Archive *fout, const ConstraintInfo *coninfo)
{
	DumpOptions *dopt = fout->dopt;
	TableInfo  *tbinfo = coninfo->contable;
	PQExpBuffer q;
	PQExpBuffer delq;
	char	   *tag = NULL;
	char	   *foreign;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	q = createPQExpBuffer();
	delq = createPQExpBuffer();

	foreign = tbinfo &&
		tbinfo->relkind == RELKIND_FOREIGN_TABLE ? "FOREIGN " : "";

	if (coninfo->contype == 'p' ||
		coninfo->contype == 'u' ||
		coninfo->contype == 'x')
	{
		/* Index-related constraint */
		IndxInfo   *indxinfo;
		int			k;

		indxinfo = (IndxInfo *) findObjectByDumpId(coninfo->conindex);

		if (indxinfo == NULL)
			pg_fatal("missing index for constraint \"%s\"",
					 coninfo->dobj.name);

		

		appendPQExpBuffer(q, "ALTER %sTABLE ONLY %s\n", foreign,
						  fmtQualifiedDumpable(tbinfo));
		appendPQExpBuffer(q, "    ADD CONSTRAINT %s ",
						  fmtId(coninfo->dobj.name));

		if (coninfo->condef)
		{
			/* pg_get_constraintdef should have provided everything */
			appendPQExpBuffer(q, "%s;\n", coninfo->condef);
		}
		else
		{
			appendPQExpBufferStr(q,
								 coninfo->contype == 'p' ? "PRIMARY KEY" : "UNIQUE");

			/*
			 * PRIMARY KEY constraints should not be using NULLS NOT DISTINCT
			 * indexes. Being able to create this was fixed, but we need to
			 * make the index distinct in order to be able to restore the
			 * dump.
			 */
			if (indxinfo->indnullsnotdistinct && coninfo->contype != 'p')
				appendPQExpBufferStr(q, " NULLS NOT DISTINCT");
			appendPQExpBufferStr(q, " (");
			for (k = 0; k < indxinfo->indnkeyattrs; k++)
			{
				int			indkey = (int) indxinfo->indkeys[k];
				const char *attname;

				if (indkey == InvalidAttrNumber)
					break;
				attname = getAttrName(indkey, tbinfo);

				appendPQExpBuffer(q, "%s%s",
								  (k == 0) ? "" : ", ",
								  fmtId(attname));
			}

			if (indxinfo->indnkeyattrs < indxinfo->indnattrs)
				appendPQExpBufferStr(q, ") INCLUDE (");

			for (k = indxinfo->indnkeyattrs; k < indxinfo->indnattrs; k++)
			{
				int			indkey = (int) indxinfo->indkeys[k];
				const char *attname;

				if (indkey == InvalidAttrNumber)
					break;
				attname = getAttrName(indkey, tbinfo);

				appendPQExpBuffer(q, "%s%s",
								  (k == indxinfo->indnkeyattrs) ? "" : ", ",
								  fmtId(attname));
			}

			appendPQExpBufferChar(q, ')');

			if (nonemptyReloptions(indxinfo->indreloptions))
			{
				appendPQExpBufferStr(q, " WITH (");
				appendReloptionsArrayAH(q, indxinfo->indreloptions, "", fout);
				appendPQExpBufferChar(q, ')');
			}

			if (coninfo->condeferrable)
			{
				appendPQExpBufferStr(q, " DEFERRABLE");
				if (coninfo->condeferred)
					appendPQExpBufferStr(q, " INITIALLY DEFERRED");
			}

			appendPQExpBufferStr(q, ";\n");
		}

		/*
		 * Append ALTER TABLE commands as needed to set properties that we
		 * only have ALTER TABLE syntax for.  Keep this in sync with the
		 * similar code in dumpIndex!
		 */

		/* If the index is clustered, we need to record that. */
		if (indxinfo->indisclustered)
		{
			appendPQExpBuffer(q, "\nALTER TABLE %s CLUSTER",
							  fmtQualifiedDumpable(tbinfo));
			/* index name is not qualified in this syntax */
			appendPQExpBuffer(q, " ON %s;\n",
							  fmtId(indxinfo->dobj.name));
		}

		/* If the index defines identity, we need to record that. */
		if (indxinfo->indisreplident)
		{
			appendPQExpBuffer(q, "\nALTER TABLE ONLY %s REPLICA IDENTITY USING",
							  fmtQualifiedDumpable(tbinfo));
			/* index name is not qualified in this syntax */
			appendPQExpBuffer(q, " INDEX %s;\n",
							  fmtId(indxinfo->dobj.name));
		}

		/* Indexes can depend on extensions */
		append_depends_on_extension(fout, q, &indxinfo->dobj,
									"pg_catalog.pg_class", "INDEX",
									fmtQualifiedDumpable(indxinfo));

		appendPQExpBuffer(delq, "ALTER %sTABLE ONLY %s ", foreign,
						  fmtQualifiedDumpable(tbinfo));
		appendPQExpBuffer(delq, "DROP CONSTRAINT %s;\n",
						  fmtId(coninfo->dobj.name));

		tag = psprintf("%s %s", tbinfo->dobj.name, coninfo->dobj.name);

		if (coninfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
			ArchiveEntry(fout, coninfo->dobj.catId, coninfo->dobj.dumpId,
						 ARCHIVE_OPTS(.tag = tag,
									  .namespace = tbinfo->dobj.namespace->dobj.name,
									  .tablespace = indxinfo->tablespace,
									  .owner = tbinfo->rolname,
									  .description = "CONSTRAINT",
									  .section = SECTION_POST_DATA,
									  .createStmt = q->data,
									  .dropStmt = delq->data));
	}
	else if (coninfo->contype == 'f')
	{
		char	   *only;

		/*
		 * Foreign keys on partitioned tables are always declared as
		 * inheriting to partitions; for all other cases, emit them as
		 * applying ONLY directly to the named table, because that's how they
		 * work for regular inherited tables.
		 */
		only = tbinfo->relkind == RELKIND_PARTITIONED_TABLE ? "" : "ONLY ";

		/*
		 * XXX Potentially wrap in a 'SET CONSTRAINTS OFF' block so that the
		 * current table data is not processed
		 */
		appendPQExpBuffer(q, "ALTER %sTABLE %s%s\n", foreign,
						  only, fmtQualifiedDumpable(tbinfo));
		appendPQExpBuffer(q, "    ADD CONSTRAINT %s %s;\n",
						  fmtId(coninfo->dobj.name),
						  coninfo->condef);

		appendPQExpBuffer(delq, "ALTER %sTABLE %s%s ", foreign,
						  only, fmtQualifiedDumpable(tbinfo));
		appendPQExpBuffer(delq, "DROP CONSTRAINT %s;\n",
						  fmtId(coninfo->dobj.name));

		tag = psprintf("%s %s", tbinfo->dobj.name, coninfo->dobj.name);

		if (coninfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
			ArchiveEntry(fout, coninfo->dobj.catId, coninfo->dobj.dumpId,
						 ARCHIVE_OPTS(.tag = tag,
									  .namespace = tbinfo->dobj.namespace->dobj.name,
									  .owner = tbinfo->rolname,
									  .description = "FK CONSTRAINT",
									  .section = SECTION_POST_DATA,
									  .createStmt = q->data,
									  .dropStmt = delq->data));
	}
	else if (coninfo->contype == 'c' && tbinfo)
	{
		/* CHECK constraint on a table */

		/* Ignore if not to be dumped separately, or if it was inherited */
		if (coninfo->separate && coninfo->conislocal)
		{
			/* not ONLY since we want it to propagate to children */
			appendPQExpBuffer(q, "ALTER %sTABLE %s\n", foreign,
							  fmtQualifiedDumpable(tbinfo));
			appendPQExpBuffer(q, "    ADD CONSTRAINT %s %s;\n",
							  fmtId(coninfo->dobj.name),
							  coninfo->condef);

			appendPQExpBuffer(delq, "ALTER %sTABLE %s ", foreign,
							  fmtQualifiedDumpable(tbinfo));
			appendPQExpBuffer(delq, "DROP CONSTRAINT %s;\n",
							  fmtId(coninfo->dobj.name));

			tag = psprintf("%s %s", tbinfo->dobj.name, coninfo->dobj.name);

			if (coninfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
				ArchiveEntry(fout, coninfo->dobj.catId, coninfo->dobj.dumpId,
							 ARCHIVE_OPTS(.tag = tag,
										  .namespace = tbinfo->dobj.namespace->dobj.name,
										  .owner = tbinfo->rolname,
										  .description = "CHECK CONSTRAINT",
										  .section = SECTION_POST_DATA,
										  .createStmt = q->data,
										  .dropStmt = delq->data));
		}
	}
	else if (coninfo->contype == 'c' && tbinfo == NULL)
	{
		/* CHECK constraint on a domain */
		TypeInfo   *tyinfo = coninfo->condomain;

		/* Ignore if not to be dumped separately */
		if (coninfo->separate)
		{
			appendPQExpBuffer(q, "ALTER DOMAIN %s\n",
							  fmtQualifiedDumpable(tyinfo));
			appendPQExpBuffer(q, "    ADD CONSTRAINT %s %s;\n",
							  fmtId(coninfo->dobj.name),
							  coninfo->condef);

			appendPQExpBuffer(delq, "ALTER DOMAIN %s ",
							  fmtQualifiedDumpable(tyinfo));
			appendPQExpBuffer(delq, "DROP CONSTRAINT %s;\n",
							  fmtId(coninfo->dobj.name));

			tag = psprintf("%s %s", tyinfo->dobj.name, coninfo->dobj.name);

			if (coninfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
				ArchiveEntry(fout, coninfo->dobj.catId, coninfo->dobj.dumpId,
							 ARCHIVE_OPTS(.tag = tag,
										  .namespace = tyinfo->dobj.namespace->dobj.name,
										  .owner = tyinfo->rolname,
										  .description = "CHECK CONSTRAINT",
										  .section = SECTION_POST_DATA,
										  .createStmt = q->data,
										  .dropStmt = delq->data));
		}
	}
	else
	{
		pg_fatal("unrecognized constraint type: %c",
				 coninfo->contype);
	}

	/* Dump Constraint Comments --- only works for table constraints */
	if (tbinfo && coninfo->separate &&
		coninfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpTableConstraintComment(fout, coninfo);

	free(tag);
	destroyPQExpBuffer(q);
	destroyPQExpBuffer(delq);
}

/*
 * dumpTableConstraintComment --- dump a constraint's comment if any
 *
 * This is split out because we need the function in two different places
 * depending on whether the constraint is dumped as part of CREATE TABLE
 * or as a separate ALTER command.
 */
static void
dumpTableConstraintComment(Archive *fout, const ConstraintInfo *coninfo)
{
	TableInfo  *tbinfo = coninfo->contable;
	PQExpBuffer conprefix = createPQExpBuffer();
	char	   *qtabname;

	qtabname = pg_strdup(fmtId(tbinfo->dobj.name));

	appendPQExpBuffer(conprefix, "CONSTRAINT %s ON",
					  fmtId(coninfo->dobj.name));

	if (coninfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, conprefix->data, qtabname,
					tbinfo->dobj.namespace->dobj.name,
					tbinfo->rolname,
					coninfo->dobj.catId, 0,
					coninfo->separate ? coninfo->dobj.dumpId : tbinfo->dobj.dumpId);

	destroyPQExpBuffer(conprefix);
	free(qtabname);
}

/*
 * dumpSequence
 *	  write the declaration (not data) of one user-defined sequence
 */
static void
dumpSequence(Archive *fout, const TableInfo *tbinfo)
{
	DumpOptions *dopt = fout->dopt;
	PGresult   *res;
	char	   *startv,
			   *incby,
			   *maxv,
			   *minv,
			   *cache,
			   *seqtype;
	bool		cycled;
	bool		is_ascending;
	int64		default_minv,
				default_maxv;
	char		bufm[32],
				bufx[32];
	PQExpBuffer query = createPQExpBuffer();
	PQExpBuffer delqry = createPQExpBuffer();
	char	   *qseqname;
	TableInfo  *owning_tab = NULL;

	qseqname = pg_strdup(fmtId(tbinfo->dobj.name));

	if (fout->remoteVersion >= 100000)
	{
		appendPQExpBuffer(query,
						  "SELECT format_type(seqtypid, NULL), "
						  "seqstart, seqincrement, "
						  "seqmax, seqmin, "
						  "seqcache, seqcycle "
						  "FROM pg_catalog.pg_sequence "
						  "WHERE seqrelid = '%u'::oid",
						  tbinfo->dobj.catId.oid);
	}
	else
	{
		/*
		 * Before PostgreSQL 10, sequence metadata is in the sequence itself.
		 *
		 * Note: it might seem that 'bigint' potentially needs to be
		 * schema-qualified, but actually that's a keyword.
		 */
		appendPQExpBuffer(query,
						  "SELECT 'bigint' AS sequence_type, "
						  "start_value, increment_by, max_value, min_value, "
						  "cache_value, is_cycled FROM %s",
						  fmtQualifiedDumpable(tbinfo));
	}

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	if (PQntuples(res) != 1)
		pg_fatal(ngettext("query to get data of sequence \"%s\" returned %d row (expected 1)",
						  "query to get data of sequence \"%s\" returned %d rows (expected 1)",
						  PQntuples(res)),
				 tbinfo->dobj.name, PQntuples(res));

	seqtype = PQgetvalue(res, 0, 0);
	startv = PQgetvalue(res, 0, 1);
	incby = PQgetvalue(res, 0, 2);
	maxv = PQgetvalue(res, 0, 3);
	minv = PQgetvalue(res, 0, 4);
	cache = PQgetvalue(res, 0, 5);
	cycled = (strcmp(PQgetvalue(res, 0, 6), "t") == 0);

	/* Calculate default limits for a sequence of this type */
	is_ascending = (incby[0] != '-');
	if (strcmp(seqtype, "smallint") == 0)
	{
		default_minv = is_ascending ? 1 : PG_INT16_MIN;
		default_maxv = is_ascending ? PG_INT16_MAX : -1;
	}
	else if (strcmp(seqtype, "integer") == 0)
	{
		default_minv = is_ascending ? 1 : PG_INT32_MIN;
		default_maxv = is_ascending ? PG_INT32_MAX : -1;
	}
	else if (strcmp(seqtype, "bigint") == 0)
	{
		default_minv = is_ascending ? 1 : PG_INT64_MIN;
		default_maxv = is_ascending ? PG_INT64_MAX : -1;
	}
	else
	{
		pg_fatal("unrecognized sequence type: %s", seqtype);
		default_minv = default_maxv = 0;	/* keep compiler quiet */
	}

	/*
	 * 64-bit strtol() isn't very portable, so convert the limits to strings
	 * and compare that way.
	 */
	snprintf(bufm, sizeof(bufm), INT64_FORMAT, default_minv);
	snprintf(bufx, sizeof(bufx), INT64_FORMAT, default_maxv);

	/* Don't print minv/maxv if they match the respective default limit */
	if (strcmp(minv, bufm) == 0)
		minv = NULL;
	if (strcmp(maxv, bufx) == 0)
		maxv = NULL;

	/*
	 * Identity sequences are not to be dropped separately.
	 */
	if (!tbinfo->is_identity_sequence)
	{
		appendPQExpBuffer(delqry, "DROP SEQUENCE %s;\n",
						  fmtQualifiedDumpable(tbinfo));
	}

	resetPQExpBuffer(query);

	

	if (tbinfo->is_identity_sequence)
	{
		owning_tab = findTableByOid(tbinfo->owning_tab);

		appendPQExpBuffer(query,
						  "ALTER TABLE %s ",
						  fmtQualifiedDumpable(owning_tab));
		appendPQExpBuffer(query,
						  "ALTER COLUMN %s ADD GENERATED ",
						  fmtId(owning_tab->attnames[tbinfo->owning_col - 1]));
		if (owning_tab->attidentity[tbinfo->owning_col - 1] == ATTRIBUTE_IDENTITY_ALWAYS)
			appendPQExpBufferStr(query, "ALWAYS");
		else if (owning_tab->attidentity[tbinfo->owning_col - 1] == ATTRIBUTE_IDENTITY_BY_DEFAULT)
			appendPQExpBufferStr(query, "BY DEFAULT");
		appendPQExpBuffer(query, " AS IDENTITY (\n    SEQUENCE NAME %s\n",
						  fmtQualifiedDumpable(tbinfo));

		/*
		 * Emit persistence option only if it's different from the owning
		 * table's.  This avoids using this new syntax unnecessarily.
		 */
		if (tbinfo->relpersistence != owning_tab->relpersistence)
			appendPQExpBuffer(query, "    %s\n",
							  tbinfo->relpersistence == RELPERSISTENCE_UNLOGGED ?
							  "UNLOGGED" : "LOGGED");
	}
	else
	{
		appendPQExpBuffer(query,
						  "CREATE %sSEQUENCE %s\n",
						  tbinfo->relpersistence == RELPERSISTENCE_UNLOGGED ?
						  "UNLOGGED " : "",
						  fmtQualifiedDumpable(tbinfo));

		if (strcmp(seqtype, "bigint") != 0)
			appendPQExpBuffer(query, "    AS %s\n", seqtype);
	}

	appendPQExpBuffer(query, "    START WITH %s\n", startv);

	appendPQExpBuffer(query, "    INCREMENT BY %s\n", incby);

	if (minv)
		appendPQExpBuffer(query, "    MINVALUE %s\n", minv);
	else
		appendPQExpBufferStr(query, "    NO MINVALUE\n");

	if (maxv)
		appendPQExpBuffer(query, "    MAXVALUE %s\n", maxv);
	else
		appendPQExpBufferStr(query, "    NO MAXVALUE\n");

	appendPQExpBuffer(query,
					  "    CACHE %s%s",
					  cache, (cycled ? "\n    CYCLE" : ""));

	if (tbinfo->is_identity_sequence)
		appendPQExpBufferStr(query, "\n);\n");
	else
		appendPQExpBufferStr(query, ";\n");

	/* binary_upgrade:	no need to clear TOAST table oid */

	

	if (tbinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, tbinfo->dobj.catId, tbinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tbinfo->dobj.name,
								  .namespace = tbinfo->dobj.namespace->dobj.name,
								  .owner = tbinfo->rolname,
								  .description = "SEQUENCE",
								  .section = SECTION_PRE_DATA,
								  .createStmt = query->data,
								  .dropStmt = delqry->data));

	/*
	 * If the sequence is owned by a table column, emit the ALTER for it as a
	 * separate TOC entry immediately following the sequence's own entry. It's
	 * OK to do this rather than using full sorting logic, because the
	 * dependency that tells us it's owned will have forced the table to be
	 * created first.  We can't just include the ALTER in the TOC entry
	 * because it will fail if we haven't reassigned the sequence owner to
	 * match the table's owner.
	 *
	 * We need not schema-qualify the table reference because both sequence
	 * and table must be in the same schema.
	 */
	if (OidIsValid(tbinfo->owning_tab) && !tbinfo->is_identity_sequence)
	{
		owning_tab = findTableByOid(tbinfo->owning_tab);

		if (owning_tab == NULL)
			pg_fatal("failed sanity check, parent table with OID %u of sequence with OID %u not found",
					 tbinfo->owning_tab, tbinfo->dobj.catId.oid);

		if (owning_tab->dobj.dump & DUMP_COMPONENT_DEFINITION)
		{
			resetPQExpBuffer(query);
			appendPQExpBuffer(query, "ALTER SEQUENCE %s",
							  fmtQualifiedDumpable(tbinfo));
			appendPQExpBuffer(query, " OWNED BY %s",
							  fmtQualifiedDumpable(owning_tab));
			appendPQExpBuffer(query, ".%s;\n",
							  fmtId(owning_tab->attnames[tbinfo->owning_col - 1]));

			if (tbinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
				ArchiveEntry(fout, nilCatalogId, createDumpId(),
							 ARCHIVE_OPTS(.tag = tbinfo->dobj.name,
										  .namespace = tbinfo->dobj.namespace->dobj.name,
										  .owner = tbinfo->rolname,
										  .description = "SEQUENCE OWNED BY",
										  .section = SECTION_PRE_DATA,
										  .createStmt = query->data,
										  .deps = &(tbinfo->dobj.dumpId),
										  .nDeps = 1));
		}
	}

	/* Dump Sequence Comments and Security Labels */
	if (tbinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "SEQUENCE", qseqname,
					tbinfo->dobj.namespace->dobj.name, tbinfo->rolname,
					tbinfo->dobj.catId, 0, tbinfo->dobj.dumpId);

	if (tbinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
		dumpSecLabel(fout, "SEQUENCE", qseqname,
					 tbinfo->dobj.namespace->dobj.name, tbinfo->rolname,
					 tbinfo->dobj.catId, 0, tbinfo->dobj.dumpId);

	PQclear(res);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(delqry);
	free(qseqname);
}

/*
 * dumpSequenceData
 *	  write the data of one user-defined sequence
 */
static void
dumpSequenceData(Archive *fout, const TableDataInfo *tdinfo)
{
	TableInfo  *tbinfo = tdinfo->tdtable;
	PGresult   *res;
	char	   *last;
	bool		called;
	PQExpBuffer query = createPQExpBuffer();

	appendPQExpBuffer(query,
					  "SELECT last_value, is_called FROM %s",
					  fmtQualifiedDumpable(tbinfo));

	res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

	if (PQntuples(res) != 1)
		pg_fatal(ngettext("query to get data of sequence \"%s\" returned %d row (expected 1)",
						  "query to get data of sequence \"%s\" returned %d rows (expected 1)",
						  PQntuples(res)),
				 tbinfo->dobj.name, PQntuples(res));

	last = PQgetvalue(res, 0, 0);
	called = (strcmp(PQgetvalue(res, 0, 1), "t") == 0);

	resetPQExpBuffer(query);
	appendPQExpBufferStr(query, "SELECT pg_catalog.setval(");
	appendStringLiteralAH(query, fmtQualifiedDumpable(tbinfo), fout);
	appendPQExpBuffer(query, ", %s, %s);\n",
					  last, (called ? "true" : "false"));

	if (tdinfo->dobj.dump & DUMP_COMPONENT_DATA)
		ArchiveEntry(fout, nilCatalogId, createDumpId(),
					 ARCHIVE_OPTS(.tag = tbinfo->dobj.name,
								  .namespace = tbinfo->dobj.namespace->dobj.name,
								  .owner = tbinfo->rolname,
								  .description = "SEQUENCE SET",
								  .section = SECTION_DATA,
								  .createStmt = query->data,
								  .deps = &(tbinfo->dobj.dumpId),
								  .nDeps = 1));

	PQclear(res);

	destroyPQExpBuffer(query);
}

/*
 * dumpTrigger
 *	  write the declaration of one user-defined table trigger
 */
static void
dumpTrigger(Archive *fout, const TriggerInfo *tginfo)
{
	DumpOptions *dopt = fout->dopt;
	TableInfo  *tbinfo = tginfo->tgtable;
	PQExpBuffer query;
	PQExpBuffer delqry;
	PQExpBuffer trigprefix;
	PQExpBuffer trigidentity;
	char	   *qtabname;
	char	   *tag;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	query = createPQExpBuffer();
	delqry = createPQExpBuffer();
	trigprefix = createPQExpBuffer();
	trigidentity = createPQExpBuffer();

	qtabname = pg_strdup(fmtId(tbinfo->dobj.name));

	appendPQExpBuffer(trigidentity, "%s ", fmtId(tginfo->dobj.name));
	appendPQExpBuffer(trigidentity, "ON %s", fmtQualifiedDumpable(tbinfo));

	appendPQExpBuffer(query, "%s;\n", tginfo->tgdef);
	appendPQExpBuffer(delqry, "DROP TRIGGER %s;\n", trigidentity->data);

	/* Triggers can depend on extensions */
	append_depends_on_extension(fout, query, &tginfo->dobj,
								"pg_catalog.pg_trigger", "TRIGGER",
								trigidentity->data);

	if (tginfo->tgispartition)
	{
		Assert(tbinfo->ispartition);

		/*
		 * Partition triggers only appear here because their 'tgenabled' flag
		 * differs from its parent's.  The trigger is created already, so
		 * remove the CREATE and replace it with an ALTER.  (Clear out the
		 * DROP query too, so that pg_dump --create does not cause errors.)
		 */
		resetPQExpBuffer(query);
		resetPQExpBuffer(delqry);
		appendPQExpBuffer(query, "\nALTER %sTABLE %s ",
						  tbinfo->relkind == RELKIND_FOREIGN_TABLE ? "FOREIGN " : "",
						  fmtQualifiedDumpable(tbinfo));
		switch (tginfo->tgenabled)
		{
			case 'f':
			case 'D':
				appendPQExpBufferStr(query, "DISABLE");
				break;
			case 't':
			case 'O':
				appendPQExpBufferStr(query, "ENABLE");
				break;
			case 'R':
				appendPQExpBufferStr(query, "ENABLE REPLICA");
				break;
			case 'A':
				appendPQExpBufferStr(query, "ENABLE ALWAYS");
				break;
		}
		appendPQExpBuffer(query, " TRIGGER %s;\n",
						  fmtId(tginfo->dobj.name));
	}
	else if (tginfo->tgenabled != 't' && tginfo->tgenabled != 'O')
	{
		appendPQExpBuffer(query, "\nALTER %sTABLE %s ",
						  tbinfo->relkind == RELKIND_FOREIGN_TABLE ? "FOREIGN " : "",
						  fmtQualifiedDumpable(tbinfo));
		switch (tginfo->tgenabled)
		{
			case 'D':
			case 'f':
				appendPQExpBufferStr(query, "DISABLE");
				break;
			case 'A':
				appendPQExpBufferStr(query, "ENABLE ALWAYS");
				break;
			case 'R':
				appendPQExpBufferStr(query, "ENABLE REPLICA");
				break;
			default:
				appendPQExpBufferStr(query, "ENABLE");
				break;
		}
		appendPQExpBuffer(query, " TRIGGER %s;\n",
						  fmtId(tginfo->dobj.name));
	}

	appendPQExpBuffer(trigprefix, "TRIGGER %s ON",
					  fmtId(tginfo->dobj.name));

	tag = psprintf("%s %s", tbinfo->dobj.name, tginfo->dobj.name);

	if (tginfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, tginfo->dobj.catId, tginfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tag,
								  .namespace = tbinfo->dobj.namespace->dobj.name,
								  .owner = tbinfo->rolname,
								  .description = "TRIGGER",
								  .section = SECTION_POST_DATA,
								  .createStmt = query->data,
								  .dropStmt = delqry->data));

	if (tginfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, trigprefix->data, qtabname,
					tbinfo->dobj.namespace->dobj.name, tbinfo->rolname,
					tginfo->dobj.catId, 0, tginfo->dobj.dumpId);

	free(tag);
	destroyPQExpBuffer(query);
	destroyPQExpBuffer(delqry);
	destroyPQExpBuffer(trigprefix);
	destroyPQExpBuffer(trigidentity);
	free(qtabname);
}

/*
 * dumpEventTrigger
 *	  write the declaration of one user-defined event trigger
 */
static void
dumpEventTrigger(Archive *fout, const EventTriggerInfo *evtinfo)
{
	DumpOptions *dopt = fout->dopt;
	PQExpBuffer query;
	PQExpBuffer delqry;
	char	   *qevtname;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	query = createPQExpBuffer();
	delqry = createPQExpBuffer();

	qevtname = pg_strdup(fmtId(evtinfo->dobj.name));

	appendPQExpBufferStr(query, "CREATE EVENT TRIGGER ");
	appendPQExpBufferStr(query, qevtname);
	appendPQExpBufferStr(query, " ON ");
	appendPQExpBufferStr(query, fmtId(evtinfo->evtevent));

	if (strcmp("", evtinfo->evttags) != 0)
	{
		appendPQExpBufferStr(query, "\n         WHEN TAG IN (");
		appendPQExpBufferStr(query, evtinfo->evttags);
		appendPQExpBufferChar(query, ')');
	}

	appendPQExpBufferStr(query, "\n   EXECUTE FUNCTION ");
	appendPQExpBufferStr(query, evtinfo->evtfname);
	appendPQExpBufferStr(query, "();\n");

	if (evtinfo->evtenabled != 'O')
	{
		appendPQExpBuffer(query, "\nALTER EVENT TRIGGER %s ",
						  qevtname);
		switch (evtinfo->evtenabled)
		{
			case 'D':
				appendPQExpBufferStr(query, "DISABLE");
				break;
			case 'A':
				appendPQExpBufferStr(query, "ENABLE ALWAYS");
				break;
			case 'R':
				appendPQExpBufferStr(query, "ENABLE REPLICA");
				break;
			default:
				appendPQExpBufferStr(query, "ENABLE");
				break;
		}
		appendPQExpBufferStr(query, ";\n");
	}

	appendPQExpBuffer(delqry, "DROP EVENT TRIGGER %s;\n",
					  qevtname);

	

	if (evtinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, evtinfo->dobj.catId, evtinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = evtinfo->dobj.name,
								  .owner = evtinfo->evtowner,
								  .description = "EVENT TRIGGER",
								  .section = SECTION_POST_DATA,
								  .createStmt = query->data,
								  .dropStmt = delqry->data));

	if (evtinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, "EVENT TRIGGER", qevtname,
					NULL, evtinfo->evtowner,
					evtinfo->dobj.catId, 0, evtinfo->dobj.dumpId);

	destroyPQExpBuffer(query);
	destroyPQExpBuffer(delqry);
	free(qevtname);
}

/*
 * dumpRule
 *		Dump a rule
 */
static void
dumpRule(Archive *fout, const RuleInfo *rinfo)
{
	DumpOptions *dopt = fout->dopt;
	TableInfo  *tbinfo = rinfo->ruletable;
	bool		is_view;
	PQExpBuffer query;
	PQExpBuffer cmd;
	PQExpBuffer delcmd;
	PQExpBuffer ruleprefix;
	char	   *qtabname;
	PGresult   *res;
	char	   *tag;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	/*
	 * If it is an ON SELECT rule that is created implicitly by CREATE VIEW,
	 * we do not want to dump it as a separate object.
	 */
	if (!rinfo->separate)
		return;

	/*
	 * If it's an ON SELECT rule, we want to print it as a view definition,
	 * instead of a rule.
	 */
	is_view = (rinfo->ev_type == '1' && rinfo->is_instead);

	query = createPQExpBuffer();
	cmd = createPQExpBuffer();
	delcmd = createPQExpBuffer();
	ruleprefix = createPQExpBuffer();

	qtabname = pg_strdup(fmtId(tbinfo->dobj.name));

	if (is_view)
	{
		PQExpBuffer result;

		/*
		 * We need OR REPLACE here because we'll be replacing a dummy view.
		 * Otherwise this should look largely like the regular view dump code.
		 */
		appendPQExpBuffer(cmd, "CREATE OR REPLACE VIEW %s",
						  fmtQualifiedDumpable(tbinfo));
		if (nonemptyReloptions(tbinfo->reloptions))
		{
			appendPQExpBufferStr(cmd, " WITH (");
			appendReloptionsArrayAH(cmd, tbinfo->reloptions, "", fout);
			appendPQExpBufferChar(cmd, ')');
		}
		result = createViewAsClause(fout, tbinfo);
		appendPQExpBuffer(cmd, " AS\n%s", result->data);
		destroyPQExpBuffer(result);
		if (tbinfo->checkoption != NULL)
			appendPQExpBuffer(cmd, "\n  WITH %s CHECK OPTION",
							  tbinfo->checkoption);
		appendPQExpBufferStr(cmd, ";\n");
	}
	else
	{
		/* In the rule case, just print pg_get_ruledef's result verbatim */
		appendPQExpBuffer(query,
						  "SELECT pg_catalog.pg_get_ruledef('%u'::pg_catalog.oid)",
						  rinfo->dobj.catId.oid);

		res = ExecuteSqlQuery(fout, query->data, PGRES_TUPLES_OK);

		if (PQntuples(res) != 1)
			pg_fatal("query to get rule \"%s\" for table \"%s\" failed: wrong number of rows returned",
					 rinfo->dobj.name, tbinfo->dobj.name);

		printfPQExpBuffer(cmd, "%s\n", PQgetvalue(res, 0, 0));

		PQclear(res);
	}

	/*
	 * Add the command to alter the rules replication firing semantics if it
	 * differs from the default.
	 */
	if (rinfo->ev_enabled != 'O')
	{
		appendPQExpBuffer(cmd, "ALTER TABLE %s ", fmtQualifiedDumpable(tbinfo));
		switch (rinfo->ev_enabled)
		{
			case 'A':
				appendPQExpBuffer(cmd, "ENABLE ALWAYS RULE %s;\n",
								  fmtId(rinfo->dobj.name));
				break;
			case 'R':
				appendPQExpBuffer(cmd, "ENABLE REPLICA RULE %s;\n",
								  fmtId(rinfo->dobj.name));
				break;
			case 'D':
				appendPQExpBuffer(cmd, "DISABLE RULE %s;\n",
								  fmtId(rinfo->dobj.name));
				break;
		}
	}

	if (is_view)
	{
		/*
		 * We can't DROP a view's ON SELECT rule.  Instead, use CREATE OR
		 * REPLACE VIEW to replace the rule with something with minimal
		 * dependencies.
		 */
		PQExpBuffer result;

		appendPQExpBuffer(delcmd, "CREATE OR REPLACE VIEW %s",
						  fmtQualifiedDumpable(tbinfo));
		result = createDummyViewAsClause(fout, tbinfo);
		appendPQExpBuffer(delcmd, " AS\n%s;\n", result->data);
		destroyPQExpBuffer(result);
	}
	else
	{
		appendPQExpBuffer(delcmd, "DROP RULE %s ",
						  fmtId(rinfo->dobj.name));
		appendPQExpBuffer(delcmd, "ON %s;\n",
						  fmtQualifiedDumpable(tbinfo));
	}

	appendPQExpBuffer(ruleprefix, "RULE %s ON",
					  fmtId(rinfo->dobj.name));

	tag = psprintf("%s %s", tbinfo->dobj.name, rinfo->dobj.name);

	if (rinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, rinfo->dobj.catId, rinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tag,
								  .namespace = tbinfo->dobj.namespace->dobj.name,
								  .owner = tbinfo->rolname,
								  .description = "RULE",
								  .section = SECTION_POST_DATA,
								  .createStmt = cmd->data,
								  .dropStmt = delcmd->data));

	/* Dump rule comments */
	if (rinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, ruleprefix->data, qtabname,
					tbinfo->dobj.namespace->dobj.name,
					tbinfo->rolname,
					rinfo->dobj.catId, 0, rinfo->dobj.dumpId);

	free(tag);
	destroyPQExpBuffer(query);
	destroyPQExpBuffer(cmd);
	destroyPQExpBuffer(delcmd);
	destroyPQExpBuffer(ruleprefix);
	free(qtabname);
}

/*
 * getExtensionMembership --- obtain extension membership data
 *
 * We need to identify objects that are extension members as soon as they're
 * loaded, so that we can correctly determine whether they need to be dumped.
 * Generally speaking, extension member objects will get marked as *not* to
 * be dumped, as they will be recreated by the single CREATE EXTENSION
 * command.  However, in binary upgrade mode we still need to dump the members
 * individually.
 */


static void
dumpLO(Archive *fout, const LoInfo *loinfo)
{
	PQExpBuffer cquery = createPQExpBuffer();

	/*
	 * The "definition" is just a newline-separated list of OIDs.  We need to
	 * put something into the dropStmt too, but it can just be a comment.
	 */
	for (int i = 0; i < loinfo->numlos; i++)
		appendPQExpBuffer(cquery, "%u\n", loinfo->looids[i]);

	if (loinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, loinfo->dobj.catId, loinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = loinfo->dobj.name,
								  .owner = loinfo->rolname,
								  .description = "BLOB METADATA",
								  .section = SECTION_DATA,
								  .createStmt = cquery->data,
								  .dropStmt = "-- dummy"));

	/*
	 * Dump per-blob comments and seclabels if any.  We assume these are rare
	 * enough that it's okay to generate retail TOC entries for them.
	 */
	if (loinfo->dobj.dump & (DUMP_COMPONENT_COMMENT |
							 DUMP_COMPONENT_SECLABEL))
	{
		for (int i = 0; i < loinfo->numlos; i++)
		{
			CatalogId	catId;
			char		namebuf[32];

			/* Build identifying info for this blob */
			catId.tableoid = loinfo->dobj.catId.tableoid;
			catId.oid = loinfo->looids[i];
			snprintf(namebuf, sizeof(namebuf), "%u", loinfo->looids[i]);

			if (loinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
				dumpComment(fout, "LARGE OBJECT", namebuf,
							NULL, loinfo->rolname,
							catId, 0, loinfo->dobj.dumpId);

			if (loinfo->dobj.dump & DUMP_COMPONENT_SECLABEL)
				dumpSecLabel(fout, "LARGE OBJECT", namebuf,
							 NULL, loinfo->rolname,
							 catId, 0, loinfo->dobj.dumpId);
		}
	}

	/*
	 * Dump the ACLs if any (remember that all blobs in the group will have
	 * the same ACL).  If there's just one blob, dump a simple ACL entry; if
	 * there's more, make a "LARGE OBJECTS" entry that really contains only
	 * the ACL for the first blob.  _printTocEntry() will be cued by the tag
	 * string to emit a mutated version for each blob.
	 */
	if (loinfo->dobj.dump & DUMP_COMPONENT_ACL)
	{
		char		namebuf[32];

		/* Build identifying info for the first blob */
		snprintf(namebuf, sizeof(namebuf), "%u", loinfo->looids[0]);

		if (loinfo->numlos > 1)
		{
			char		tagbuf[64];

			snprintf(tagbuf, sizeof(tagbuf), "LARGE OBJECTS %u..%u",
					 loinfo->looids[0], loinfo->looids[loinfo->numlos - 1]);

			dumpACL(fout, loinfo->dobj.dumpId, InvalidDumpId,
					"LARGE OBJECT", namebuf, NULL, NULL,
					tagbuf, loinfo->rolname, &loinfo->dacl);
		}
		else
		{
			dumpACL(fout, loinfo->dobj.dumpId, InvalidDumpId,
					"LARGE OBJECT", namebuf, NULL, NULL,
					NULL, loinfo->rolname, &loinfo->dacl);
		}
	}

	destroyPQExpBuffer(cquery);
}

static void
refreshMatViewData(Archive *fout, const TableDataInfo *tdinfo)
{
	TableInfo  *tbinfo = tdinfo->tdtable;
	PQExpBuffer q;

	/* If the materialized view is not flagged as populated, skip this. */
	if (!tbinfo->relispopulated)
		return;

	q = createPQExpBuffer();

	appendPQExpBuffer(q, "REFRESH MATERIALIZED VIEW %s;\n",
					  fmtQualifiedDumpable(tbinfo));

	if (tdinfo->dobj.dump & DUMP_COMPONENT_DATA)
		ArchiveEntry(fout,
					 tdinfo->dobj.catId,	/* catalog ID */
					 tdinfo->dobj.dumpId,	/* dump ID */
					 ARCHIVE_OPTS(.tag = tbinfo->dobj.name,
								  .namespace = tbinfo->dobj.namespace->dobj.name,
								  .owner = tbinfo->rolname,
								  .description = "MATERIALIZED VIEW DATA",
								  .section = SECTION_POST_DATA,
								  .createStmt = q->data,
								  .deps = tdinfo->dobj.dependencies,
								  .nDeps = tdinfo->dobj.nDeps));

	destroyPQExpBuffer(q);
}

/*
 * dumpLOs:
 *	dump the data contents of the large objects in the given group
 */
static int
dumpLOs(Archive *fout, const void *arg)
{
	const LoInfo *loinfo = (const LoInfo *) arg;
	PGconn	   *conn = GetConnection(fout);
	char		buf[LOBBUFSIZE];

	pg_log_info("saving large objects \"%s\"", loinfo->dobj.name);

	for (int i = 0; i < loinfo->numlos; i++)
	{
		Oid			loOid = loinfo->looids[i];
		int			loFd;
		int			cnt;

		/* Open the LO */
		loFd = lo_open(conn, loOid, INV_READ);
		if (loFd == -1)
			pg_fatal("could not open large object %u: %s",
					 loOid, PQerrorMessage(conn));

		StartLO(fout, loOid);

		/* Now read it in chunks, sending data to archive */
		do
		{
			cnt = lo_read(conn, loFd, buf, LOBBUFSIZE);
			if (cnt < 0)
				pg_fatal("error reading large object %u: %s",
						 loOid, PQerrorMessage(conn));

			WriteData(fout, buf, cnt);
		} while (cnt > 0);

		lo_close(conn, loFd);

		EndLO(fout, loOid);
	}

	return 1;
}

void
dumpDumpableObject(Archive *fout, DumpableObject *dobj)
{
	/*
	 * Clear any dump-request bits for components that don't exist for this
	 * object.  (This makes it safe to initially use DUMP_COMPONENT_ALL as the
	 * request for every kind of object.)
	 */
	dobj->dump &= dobj->components;

	/* Now, short-circuit if there's nothing to be done here. */
	if (dobj->dump == 0)
		return;

	switch (dobj->objType)
	{
		case DO_NAMESPACE:
			dumpNamespace(fout, (const NamespaceInfo *) dobj);
			break;
		case DO_EXTENSION:
			if (!no_exts) {
				dumpExtension(fout, (const ExtensionInfo *) dobj);
				break;
			} else {
				break;
			}
		case DO_TYPE:
			dumpType(fout, (const TypeInfo *) dobj);
			break;
		case DO_SHELL_TYPE:
			dumpShellType(fout, (const ShellTypeInfo *) dobj);
			break;
		case DO_FUNC:
			dumpFunc(fout, (const FuncInfo *) dobj);
			break;
		case DO_AGG:
			dumpAgg(fout, (const AggInfo *) dobj);
			break;
		case DO_OPERATOR:
			dumpOpr(fout, (const OprInfo *) dobj);
			break;
		case DO_ACCESS_METHOD:
			dumpAccessMethod(fout, (const AccessMethodInfo *) dobj);
			break;
		case DO_OPCLASS:
			dumpOpclass(fout, (const OpclassInfo *) dobj);
			break;
		case DO_OPFAMILY:
			dumpOpfamily(fout, (const OpfamilyInfo *) dobj);
			break;
		case DO_COLLATION:
			dumpCollation(fout, (const CollInfo *) dobj);
			break;
		case DO_CONVERSION:
			dumpConversion(fout, (const ConvInfo *) dobj);
			break;
		case DO_TABLE:
			dumpTable(fout, (const TableInfo *) dobj);
			break;
		case DO_TABLE_ATTACH:
			dumpTableAttach(fout, (const TableAttachInfo *) dobj);
			break;
		case DO_ATTRDEF:
			dumpAttrDef(fout, (const AttrDefInfo *) dobj);
			break;
		case DO_INDEX:
			dumpIndex(fout, (const IndxInfo *) dobj);
			break;
		case DO_INDEX_ATTACH:
			dumpIndexAttach(fout, (const IndexAttachInfo *) dobj);
			break;
		case DO_STATSEXT:
			dumpStatisticsExt(fout, (const StatsExtInfo *) dobj);
			break;
		case DO_REFRESH_MATVIEW:
			refreshMatViewData(fout, (const TableDataInfo *) dobj);
			break;
		case DO_RULE:
			dumpRule(fout, (const RuleInfo *) dobj);
			break;
		case DO_TRIGGER:
			dumpTrigger(fout, (const TriggerInfo *) dobj);
			break;
		case DO_EVENT_TRIGGER:
			dumpEventTrigger(fout, (const EventTriggerInfo *) dobj);
			break;
		case DO_CONSTRAINT:
			dumpConstraint(fout, (const ConstraintInfo *) dobj);
			break;
		case DO_FK_CONSTRAINT:
			dumpConstraint(fout, (const ConstraintInfo *) dobj);
			break;
		case DO_PROCLANG:
			dumpProcLang(fout, (const ProcLangInfo *) dobj);
			break;
		case DO_CAST:
			dumpCast(fout, (const CastInfo *) dobj);
			break;
		case DO_TRANSFORM:
			dumpTransform(fout, (const TransformInfo *) dobj);
			break;
		case DO_SEQUENCE_SET:
			//dumpSequenceData(fout, (const TableDataInfo *) dobj);
			break;
		case DO_DUMMY_TYPE:
			/* table rowtypes and array types are never dumped separately */
			break;
		case DO_TSPARSER:
			dumpTSParser(fout, (const TSParserInfo *) dobj);
			break;
		case DO_TSDICT:
			dumpTSDictionary(fout, (const TSDictInfo *) dobj);
			break;
		case DO_TSTEMPLATE:
			dumpTSTemplate(fout, (const TSTemplateInfo *) dobj);
			break;
		case DO_TSCONFIG:
			dumpTSConfig(fout, (const TSConfigInfo *) dobj);
			break;
		case DO_FDW:
			dumpForeignDataWrapper(fout, (const FdwInfo *) dobj);
			break;
		case DO_FOREIGN_SERVER:
			dumpForeignServer(fout, (const ForeignServerInfo *) dobj);
			break;
		case DO_DEFAULT_ACL:
			dumpDefaultACL(fout, (const DefaultACLInfo *) dobj);
			break;
		case DO_LARGE_OBJECT:
			dumpLO(fout, (const LoInfo *) dobj);
			break;
		case DO_LARGE_OBJECT_DATA:
			if (dobj->dump & DUMP_COMPONENT_DATA)
			{
				LoInfo	   *loinfo;
				TocEntry   *te;

				loinfo = (LoInfo *) findObjectByDumpId(dobj->dependencies[0]);
				if (loinfo == NULL)
					pg_fatal("missing metadata for large objects \"%s\"",
							 dobj->name);

				te = ArchiveEntry(fout, dobj->catId, dobj->dumpId,
								  ARCHIVE_OPTS(.tag = dobj->name,
											   .owner = loinfo->rolname,
											   .description = "BLOBS",
											   .section = SECTION_DATA,
											   .deps = dobj->dependencies,
											   .nDeps = dobj->nDeps,
											   .dumpFn = dumpLOs,
											   .dumpArg = loinfo));

				/*
				 * Set the TocEntry's dataLength in case we are doing a
				 * parallel dump and want to order dump jobs by table size.
				 * (We need some size estimate for every TocEntry with a
				 * DataDumper function.)  We don't currently have any cheap
				 * way to estimate the size of LOs, but fortunately it doesn't
				 * matter too much as long as we get large batches of LOs
				 * processed reasonably early.  Assume 8K per blob.
				 */
				te->dataLength = loinfo->numlos * (pgoff_t) 8192;
			}
			break;
		case DO_POLICY:
			dumpPolicy(fout, (const PolicyInfo *) dobj);
			break;
		case DO_PUBLICATION:
			dumpPublication(fout, (const PublicationInfo *) dobj);
			break;
		case DO_PUBLICATION_REL:
			dumpPublicationTable(fout, (const PublicationRelInfo *) dobj);
			break;
		case DO_PUBLICATION_TABLE_IN_SCHEMA:
			dumpPublicationNamespace(fout,
									 (const PublicationSchemaInfo *) dobj);
			break;
		case DO_SUBSCRIPTION:
			dumpSubscription(fout, (const SubscriptionInfo *) dobj);
			break;
		case DO_SUBSCRIPTION_REL:
			dumpSubscriptionTable(fout, (const SubRelInfo *) dobj);
			break;
		case DO_PRE_DATA_BOUNDARY:
		case DO_POST_DATA_BOUNDARY:
			/* never dumped, nothing to do */
			break;
	}
}


static void
dumpCommentExtended(Archive *fout, const char *type,
					const char *name, const char *namespace,
					const char *owner, CatalogId catalogId,
					int subid, DumpId dumpId,
					const char *initdb_comment)
{
	DumpOptions *dopt = fout->dopt;
	CommentItem *comments;
	int			ncomments;

	/* do nothing, if --no-comments is supplied */
	if (dopt->no_comments)
		return;

	/* Comments are schema not data ... except LO comments are data */
	if (strcmp(type, "LARGE OBJECT") != 0)
	{
		if (dopt->dataOnly)
			return;
	}
	else
	{
		/* We do dump LO comments in binary-upgrade mode */
		if (dopt->schemaOnly && !dopt->binary_upgrade)
			return;
	}
    return;
	/* Search for comments associated with catalogId, using table */
	ncomments = findComments(catalogId.tableoid, catalogId.oid,
							 &comments);

	/* Is there one matching the subid? */
	while (ncomments > 0)
	{
		if (comments->objsubid == subid)
			break;
		comments++;
		ncomments--;
	}

	if (initdb_comment != NULL)
	{
		static CommentItem empty_comment = {.descr = ""};

		/*
		 * initdb creates this object with a comment.  Skip dumping the
		 * initdb-provided comment, which would complicate matters for
		 * non-superuser use of pg_dump.  When the DBA has removed initdb's
		 * comment, replicate that.
		 */
		if (ncomments == 0)
		{
			comments = &empty_comment;
			ncomments = 1;
		}
		else if (strcmp(comments->descr, initdb_comment) == 0)
			ncomments = 0;
	}

	/* If a comment exists, build COMMENT ON statement */
	if (ncomments > 0)
	{
		PQExpBuffer query = createPQExpBuffer();
		PQExpBuffer tag = createPQExpBuffer();

		appendPQExpBuffer(query, "COMMENT ON %s ", type);
		if (namespace && *namespace)
			appendPQExpBuffer(query, "%s.", fmtId(namespace));
		appendPQExpBuffer(query, "%s IS ", name);
		appendStringLiteralAH(query, comments->descr, fout);
		appendPQExpBufferStr(query, ";\n");

		appendPQExpBuffer(tag, "%s %s", type, name);

		/*
		 * We mark comments as SECTION_NONE because they really belong in the
		 * same section as their parent, whether that is pre-data or
		 * post-data.
		 */
		ArchiveEntry(fout, nilCatalogId, createDumpId(),
					 ARCHIVE_OPTS(.tag = tag->data,
								  .namespace = namespace,
								  .owner = owner,
								  .description = "COMMENT",
								  .section = SECTION_NONE,
								  .createStmt = query->data,
								  .deps = &dumpId,
								  .nDeps = 1));

		destroyPQExpBuffer(query);
		destroyPQExpBuffer(tag);
	}
}

static inline void
dumpComment(Archive *fout, const char *type,
			const char *name, const char *namespace,
			const char *owner, CatalogId catalogId,
			int subid, DumpId dumpId)
{
	dumpCommentExtended(fout, type, name, namespace, owner,
						catalogId, subid, dumpId, NULL);
}



static void
dumpPolicy(Archive *fout, const PolicyInfo *polinfo)
{
	DumpOptions *dopt = fout->dopt;
	TableInfo  *tbinfo = polinfo->poltable;
	PQExpBuffer query;
	PQExpBuffer delqry;
	PQExpBuffer polprefix;
	char	   *qtabname;
	const char *cmd;
	char	   *tag;

	/* Do nothing in data-only dump */
	if (dopt->dataOnly)
		return;

	/*
	 * If polname is NULL, then this record is just indicating that ROW LEVEL
	 * SECURITY is enabled for the table. Dump as ALTER TABLE <table> ENABLE
	 * ROW LEVEL SECURITY.
	 */
	if (polinfo->polname == NULL)
	{
		query = createPQExpBuffer();

		appendPQExpBuffer(query, "ALTER TABLE %s ENABLE ROW LEVEL SECURITY;",
						  fmtQualifiedDumpable(tbinfo));

		/*
		 * We must emit the ROW SECURITY object's dependency on its table
		 * explicitly, because it will not match anything in pg_depend (unlike
		 * the case for other PolicyInfo objects).
		 */
		if (polinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
			ArchiveEntry(fout, polinfo->dobj.catId, polinfo->dobj.dumpId,
						 ARCHIVE_OPTS(.tag = polinfo->dobj.name,
									  .namespace = polinfo->dobj.namespace->dobj.name,
									  .owner = tbinfo->rolname,
									  .description = "ROW SECURITY",
									  .section = SECTION_POST_DATA,
									  .createStmt = query->data,
									  .deps = &(tbinfo->dobj.dumpId),
									  .nDeps = 1));

		destroyPQExpBuffer(query);
		return;
	}

	if (polinfo->polcmd == '*')
		cmd = "";
	else if (polinfo->polcmd == 'r')
		cmd = " FOR SELECT";
	else if (polinfo->polcmd == 'a')
		cmd = " FOR INSERT";
	else if (polinfo->polcmd == 'w')
		cmd = " FOR UPDATE";
	else if (polinfo->polcmd == 'd')
		cmd = " FOR DELETE";
	else
		pg_fatal("unexpected policy command type: %c",
				 polinfo->polcmd);

	query = createPQExpBuffer();
	delqry = createPQExpBuffer();
	polprefix = createPQExpBuffer();

	qtabname = pg_strdup(fmtId(tbinfo->dobj.name));

	appendPQExpBuffer(query, "CREATE POLICY %s", fmtId(polinfo->polname));

	appendPQExpBuffer(query, " ON %s%s%s", fmtQualifiedDumpable(tbinfo),
					  !polinfo->polpermissive ? " AS RESTRICTIVE" : "", cmd);

	if (polinfo->polroles != NULL)
		appendPQExpBuffer(query, " TO %s", polinfo->polroles);

	if (polinfo->polqual != NULL)
		appendPQExpBuffer(query, " USING (%s)", polinfo->polqual);

	if (polinfo->polwithcheck != NULL)
		appendPQExpBuffer(query, " WITH CHECK (%s)", polinfo->polwithcheck);

	appendPQExpBufferStr(query, ";\n");

	appendPQExpBuffer(delqry, "DROP POLICY %s", fmtId(polinfo->polname));
	appendPQExpBuffer(delqry, " ON %s;\n", fmtQualifiedDumpable(tbinfo));

	appendPQExpBuffer(polprefix, "POLICY %s ON",
					  fmtId(polinfo->polname));

	tag = psprintf("%s %s", tbinfo->dobj.name, polinfo->dobj.name);

	if (polinfo->dobj.dump & DUMP_COMPONENT_DEFINITION)
		ArchiveEntry(fout, polinfo->dobj.catId, polinfo->dobj.dumpId,
					 ARCHIVE_OPTS(.tag = tag,
								  .namespace = polinfo->dobj.namespace->dobj.name,
								  .owner = tbinfo->rolname,
								  .description = "POLICY",
								  .section = SECTION_POST_DATA,
								  .createStmt = query->data,
								  .dropStmt = delqry->data));

	if (polinfo->dobj.dump & DUMP_COMPONENT_COMMENT)
		dumpComment(fout, polprefix->data, qtabname,
					tbinfo->dobj.namespace->dobj.name, tbinfo->rolname,
					polinfo->dobj.catId, 0, polinfo->dobj.dumpId);

	free(tag);
	destroyPQExpBuffer(query);
	destroyPQExpBuffer(delqry);
	destroyPQExpBuffer(polprefix);
	free(qtabname);
}