#include "pg_ozerki.h"
#include "common/logging.h"
#include "fe_utils/string_utils.h"
#include "common/connect.h"
#include "common/string.h"
#include "postgres_fe.h"

#include <unistd.h>
#include <ctype.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#define WRITE_ERROR_EXIT \
	do { \
		pg_fatal("could not write to output file: %m"); \
	} while (0)

#define appendByteaLiteralAHX(buf,str,len,AH) \
	appendByteaLiteral(buf, str, len, (AH)->public.std_strings)




void
warn_or_exit_horribly(ArchiveHandle *AH, const char *fmt,...);

void
InitDumpOptions(DumpOptions *opts)
{
	memset(opts, 0, sizeof(DumpOptions));
	/* set any fields that shouldn't default to zeroes */
	opts->include_everything = true;
	opts->cparams.promptPassword = TRI_DEFAULT;
	opts->dumpSections = DUMP_UNSECTIONED;
}

bool
read_none(void *ptr, size_t size, size_t *rsize, CompressFileHandle *CFH)
{
	FILE	   *fp = (FILE *) CFH->private_data;
	size_t		ret;

	if (size == 0)
		return true;

	ret = fread(ptr, 1, size, fp);
	if (ret != size && !feof(fp))
		pg_fatal("could not read from input file: %m");

	if (rsize)
		*rsize = ret;

	return true;
}

bool
write_none(const void *ptr, size_t size, CompressFileHandle *CFH)
{
	size_t		ret;

	ret = fwrite(ptr, 1, size, (FILE *) CFH->private_data);
	if (ret != size)
		return false;

	return true;
}

const char *
get_error_none(CompressFileHandle *CFH)
{
	return strerror(errno);
}

char *
gets_none(char *ptr, int size, CompressFileHandle *CFH)
{
	return fgets(ptr, size, (FILE *) CFH->private_data);
}

int
getc_none(CompressFileHandle *CFH)
{
	FILE	   *fp = (FILE *) CFH->private_data;
	int			ret;

	ret = fgetc(fp);
	if (ret == EOF)
	{
		if (!feof(fp))
			pg_fatal("could not read from input file: %m");
		else
			pg_fatal("could not read from input file: end of file");
	}

	return ret;
}

bool
close_none(CompressFileHandle *CFH)
{
	FILE	   *fp = (FILE *) CFH->private_data;
	int			ret = 0;

	CFH->private_data = NULL;

	if (fp)
		ret = fclose(fp);

	return ret == 0;
}

bool
eof_none(CompressFileHandle *CFH)
{
	return feof((FILE *) CFH->private_data) != 0;
}

bool
open_none(const char *path, int fd, const char *mode, CompressFileHandle *CFH)
{
	Assert(CFH->private_data == NULL);

	if (fd >= 0)
		CFH->private_data = fdopen(dup(fd), mode);
	else
		CFH->private_data = fopen(path, mode);

	if (CFH->private_data == NULL)
		return false;

	return true;
}

bool
open_write_none(const char *path, const char *mode, CompressFileHandle *CFH)
{
	Assert(CFH->private_data == NULL);

	CFH->private_data = fopen(path, mode);
	if (CFH->private_data == NULL)
		return false;

	return true;
}


int
ahprintf(ArchiveHandle *AH, const char *fmt,...)
{
	int			save_errno = errno;
	char	   *p;
	size_t		len = 128;		/* initial assumption about buffer size */
	size_t		cnt;

	for (;;)
	{
		va_list		args;

		/* Allocate work buffer. */
		p = (char *) pg_malloc(len);

		/* Try to format the data. */
		errno = save_errno;
		va_start(args, fmt);
		cnt = pvsnprintf(p, len, fmt, args);
		va_end(args);

		if (cnt < len)
			break;				/* success */

		/* Release buffer and loop around to try again with larger len. */
		free(p);
		len = cnt;
	}

	ahwrite(p, 1, cnt, AH);
	free(p);
	return (int) cnt;
}

void
dump_lo_buf(ArchiveHandle *AH)
{
	if (AH->connection)
	{
		int			res;

		res = lo_write(AH->connection, AH->loFd, AH->lo_buf, AH->lo_buf_used);
		pg_log_debug(ngettext("wrote %zu byte of large object data (result = %d)",
							  "wrote %zu bytes of large object data (result = %d)",
							  AH->lo_buf_used),
					 AH->lo_buf_used, res);
		/* We assume there are no short writes, only errors */
		if (res != AH->lo_buf_used)
			warn_or_exit_horribly(AH, "could not write to large object: %s",
								  PQerrorMessage(AH->connection));
	}
	else
	{
		PQExpBuffer buf = createPQExpBuffer();

		appendByteaLiteralAHX(buf,
							  (const unsigned char *) AH->lo_buf,
							  AH->lo_buf_used,
							  AH);

		/* Hack: turn off writingLO so ahwrite doesn't recurse to here */
		AH->writingLO = false;
		ahprintf(AH, "SELECT pg_catalog.lowrite(0, %s);\n", buf->data);
		AH->writingLO = true;

		destroyPQExpBuffer(buf);
	}
	AH->lo_buf_used = 0;
}

void
ahwrite(const void *ptr, size_t size, size_t nmemb, ArchiveHandle *AH)
{
	int			bytes_written = 0;

	if (AH->writingLO)
	{
		size_t		remaining = size * nmemb;

		while (AH->lo_buf_used + remaining > AH->lo_buf_size)
		{
			size_t		avail = AH->lo_buf_size - AH->lo_buf_used;

			memcpy((char *) AH->lo_buf + AH->lo_buf_used, ptr, avail);
			ptr = (const void *) ((const char *) ptr + avail);
			remaining -= avail;
			AH->lo_buf_used += avail;
			dump_lo_buf(AH);
		}

		memcpy((char *) AH->lo_buf + AH->lo_buf_used, ptr, remaining);
		AH->lo_buf_used += remaining;

		bytes_written = size * nmemb;
	}
	else if (AH->CustomOutPtr)
		bytes_written = AH->CustomOutPtr(AH, ptr, size * nmemb);

	/*
	 * If we're doing a restore, and it's direct to DB, and we're connected
	 * then send it to the DB.
	 */
	else
	{
		CompressFileHandle *CFH = (CompressFileHandle *) AH->OF;

		if (CFH->write_func(ptr, size * nmemb, CFH))
			bytes_written = size * nmemb;
	}

	if (bytes_written != size * nmemb)
		WRITE_ERROR_EXIT;
}


void
_WriteData(ArchiveHandle *AH, const void *data, size_t dLen)
{
	/* Just send it to output, ahwrite() already errors on failure */
	ahwrite(data, 1, dLen, AH);
}

/*
 * Called by dumper via archiver from within a data dump routine
 * We substitute this for _WriteData while emitting a LO
 */
void
_WriteLOData(ArchiveHandle *AH, const void *data, size_t dLen)
{
	if (dLen > 0)
	{
		PQExpBuffer buf = createPQExpBuffer();

		appendByteaLiteralAHX(buf,
							  (const unsigned char *) data,
							  dLen,
							  AH);

		ahprintf(AH, "SELECT pg_catalog.lowrite(0, %s);\n", buf->data);

		destroyPQExpBuffer(buf);
	}
}

void
_EndData(ArchiveHandle *AH, TocEntry *te)
{
	ahprintf(AH, "\n\n");
}

/*
 * Called by the archiver when starting to save BLOB DATA (not schema).
 * This routine should save whatever format-specific information is needed
 * to read the LOs back into memory.
 *
 * It is called just prior to the dumper's DataDumper routine.
 *
 * Optional, but strongly recommended.
 */
void
_StartLOs(ArchiveHandle *AH, TocEntry *te)
{
	ahprintf(AH, "BEGIN;\n\n");
}

/*
 * Called by the archiver when the dumper calls StartLO.
 *
 * Mandatory.
 *
 * Must save the passed OID for retrieval at restore-time.
 */
void
_StartLO(ArchiveHandle *AH, TocEntry *te, Oid oid)
{
	bool		old_lo_style = 0;

	if (oid == 0)
		pg_fatal("invalid OID for large object");

	
	ahprintf(AH, "SELECT pg_catalog.lo_open('%u', %d);\n",
				 oid, INV_WRITE);

	AH->WriteDataPtr = _WriteLOData;
}


/*
 * Called by the archiver when the dumper calls EndLO.
 *
 * Optional.
 */
void
_EndLO(ArchiveHandle *AH, TocEntry *te, Oid oid)
{
	AH->WriteDataPtr = _WriteData;

	ahprintf(AH, "SELECT pg_catalog.lo_close(0);\n\n");
}

/*
 * Called by the archiver when finishing saving BLOB DATA.
 *
 * Optional.
 */
void
_EndLOs(ArchiveHandle *AH, TocEntry *te)
{
	ahprintf(AH, "COMMIT;\n\n");
}

/*------
 * Called as part of a RestoreArchive call; for the NULL archive, this
 * just sends the data for a given TOC entry to the output.
 *------
 */
void
_PrintTocData(ArchiveHandle *AH, TocEntry *te)
{
	if (te->dataDumper)
	{
		AH->currToc = te;

		if (strcmp(te->desc, "BLOBS") == 0)
			_StartLOs(AH, te);

		te->dataDumper((Archive *) AH, te->dataDumperArg);

		if (strcmp(te->desc, "BLOBS") == 0)
			_EndLOs(AH, te);

		AH->currToc = NULL;
	}
}

int
_WriteByte(ArchiveHandle *AH, const int i)
{
	/* Don't do anything */
	return 0;
}

void
_WriteBuf(ArchiveHandle *AH, const void *buf, size_t len)
{
	/* Don't do anything */
}

void
_CloseArchive(ArchiveHandle *AH)
{
	/* Nothing to do */
}


static ArchiveHandle *
_allocAH(const char *FileSpec, const ArchiveFormat fmt,
		 const pg_compress_specification compression_spec,
		 bool dosync, ArchiveMode mode,
		 SetupWorkerPtrType setupWorkerPtr, DataDirSyncMethod sync_method)
{
	ArchiveHandle *AH;
	CompressFileHandle *CFH;
	pg_compress_specification out_compress_spec = {0};



	AH = (ArchiveHandle *) pg_malloc0(sizeof(ArchiveHandle));

	AH->stage = STAGE_INITIALIZING;
	AH->lastErrorStage = STAGE_NONE;

	AH->version = K_VERS_SELF;

	/* initialize for backwards compatible string processing */
	AH->public.encoding = 0;	/* PG_SQL_ASCII */
	AH->public.std_strings = false;


	/* sql error handling */
	AH->public.exit_on_error = true;
	AH->public.n_errors = 0;

	AH->archiveDumpVersion = PG_VERSION;

	AH->createDate = time(NULL);

	AH->intSize = sizeof(int);
	AH->offSize = sizeof(pgoff_t);
	if (FileSpec)
	{
		AH->fSpec = pg_strdup(FileSpec);

		/*
		 * Not used; maybe later....
		 *
		 * AH->workDir = pg_strdup(FileSpec); for(i=strlen(FileSpec) ; i > 0 ;
		 * i--) if (AH->workDir[i-1] == '/')
		 */
	}
	else
		AH->fSpec = NULL;

	AH->currUser = NULL;		/* unknown */
	AH->currSchema = NULL;		/* ditto */
	AH->currTablespace = NULL;	/* ditto */
	AH->currTableAm = NULL;		/* ditto */

	AH->toc = (TocEntry *) pg_malloc0(sizeof(TocEntry));

	AH->toc->next = AH->toc;
	AH->toc->prev = AH->toc;

	AH->mode = mode;
	AH->compression_spec = compression_spec;
	AH->dosync = dosync;
	AH->sync_method = sync_method;

	memset(&(AH->sqlparse), 0, sizeof(AH->sqlparse));
	CFH = pg_malloc0(sizeof(CompressFileHandle));
	/* Open stdout with no compression for AH output handle */
	out_compress_spec.algorithm = PG_COMPRESSION_NONE;
	InitCompressFileHandleNone(CFH, compression_spec);
	if (!CFH->open_func(NULL, fileno(stdout), PG_BINARY_A, CFH))
		pg_fatal("could not open stdout for appending: %m");
	AH->OF = CFH;

	AH->SetupWorkerPtr = setupWorkerPtr;

	AH->WriteDataPtr = _WriteData;
	AH->EndDataPtr = _EndData;
	AH->WriteBytePtr = _WriteByte;
	AH->WriteBufPtr = _WriteBuf;
	AH->ClosePtr = _CloseArchive;
	AH->ReopenPtr = NULL;
	AH->PrintTocDataPtr = _PrintTocData;

	AH->StartLOsPtr = _StartLOs;
	AH->StartLOPtr = _StartLO;
	AH->EndLOPtr = _EndLO;
	AH->EndLOsPtr = _EndLOs;
	AH->ClonePtr = NULL;
	AH->DeClonePtr = NULL;


	return AH;
}

Archive *
CreateArchive(const char *FileSpec, const ArchiveFormat fmt,
			  const pg_compress_specification compression_spec,
			  bool dosync, ArchiveMode mode,
			  SetupWorkerPtrType setupDumpWorker,
			  DataDirSyncMethod sync_method)

{
	ArchiveHandle *AH = _allocAH(FileSpec, fmt, compression_spec,
								 dosync, mode, setupDumpWorker, sync_method);

	return (Archive *) AH;
}

void
SetArchiveOptions(Archive *AH, DumpOptions *dopt, RestoreOptions *ropt)
{

	/* Save options for later access */
	AH->dopt = dopt;
	AH->ropt = ropt;
}

static void
notice_processor(void *arg, const char *message)
{
	pg_log_info("%s", message);
}

void
warn_or_exit_horribly(ArchiveHandle *AH, const char *fmt,...)
{
	va_list		ap;

	switch (AH->stage)
	{

		case STAGE_NONE:
			/* Do nothing special */
			break;

		case STAGE_INITIALIZING:
			if (AH->stage != AH->lastErrorStage)
				pg_log_info("while INITIALIZING:");
			break;

		case STAGE_PROCESSING:
			if (AH->stage != AH->lastErrorStage)
				pg_log_info("while PROCESSING TOC:");
			break;

		case STAGE_FINALIZING:
			if (AH->stage != AH->lastErrorStage)
				pg_log_info("while FINALIZING:");
			break;
	}
	if (AH->currentTE != NULL && AH->currentTE != AH->lastErrorTE)
	{
		pg_log_info("from TOC entry %d; %u %u %s %s %s",
					AH->currentTE->dumpId,
					AH->currentTE->catalogId.tableoid,
					AH->currentTE->catalogId.oid,
					AH->currentTE->desc ? AH->currentTE->desc : "(no desc)",
					AH->currentTE->tag ? AH->currentTE->tag : "(no tag)",
					AH->currentTE->owner ? AH->currentTE->owner : "(no owner)");
	}
	AH->lastErrorStage = AH->stage;
	AH->lastErrorTE = AH->currentTE;

	va_start(ap, fmt);
	pg_log_generic_v(PG_LOG_ERROR, PG_LOG_PRIMARY, fmt, ap);
	va_end(ap);

	if (AH->public.exit_on_error)
		exit(1);
	else
		AH->public.n_errors++;
}

static void
_check_database_version(ArchiveHandle *AH)
{
	const char *remoteversion_str;
	int			remoteversion;
	PGresult   *res;

	remoteversion_str = PQparameterStatus(AH->connection, "server_version");
	remoteversion = PQserverVersion(AH->connection);
	if (remoteversion == 0 || !remoteversion_str)
		pg_fatal("could not get server_version from libpq");

	AH->public.remoteVersionStr = pg_strdup(remoteversion_str);
	AH->public.remoteVersion = remoteversion;
	if (!AH->archiveRemoteVersion)
		AH->archiveRemoteVersion = AH->public.remoteVersionStr;

	if (remoteversion != PG_VERSION_NUM
		&& (remoteversion < AH->public.minRemoteVersion ||
			remoteversion > AH->public.maxRemoteVersion))
	{
		pg_log_error("aborting because of server version mismatch");
		pg_log_error_detail("server version: %s; %s version: %s",
							remoteversion_str, progname, PG_VERSION);
		exit(1);
	}

	/*
	 * Check if server is in recovery mode, which means we are on a hot
	 * standby.
	 */
	res = ExecuteSqlQueryForSingleRow((Archive *) AH,
									  "SELECT pg_catalog.pg_is_in_recovery()");
	AH->public.isStandby = (strcmp(PQgetvalue(res, 0, 0), "t") == 0);
	PQclear(res);
}





void
ConnectDatabase(Archive *AHX,
				const ConnParams *cparams,
				bool isReconnect)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;
	trivalue	prompt_password;
	char	   *password;
	bool		new_pass;
	
	if (AH->connection)
		pg_fatal("already connected to a database");

	/* Never prompt for a password during a reconnection */
	prompt_password = isReconnect ? TRI_NO : cparams->promptPassword;

	password = AH->savedPassword;

	if (prompt_password == TRI_YES && password == NULL)
		password = simple_prompt("Password: ", false);

	/*
	 * Start the connection.  Loop until we have a password if requested by
	 * backend.
	 */
	
	do
	{
		const char *keywords[8];
		const char *values[8];
		int			i = 0;

		/*
		 * If dbname is a connstring, its entries can override the other
		 * values obtained from cparams; but in turn, override_dbname can
		 * override the dbname component of it.
		 */
		keywords[i] = "host";
		values[i++] = cparams->pghost;
		keywords[i] = "port";
		values[i++] = cparams->pgport;
		keywords[i] = "user";
		values[i++] = cparams->username;
		keywords[i] = "password";
		values[i++] = password;
		keywords[i] = "dbname";
		values[i++] = cparams->dbname;
		if (cparams->override_dbname)
		{
			keywords[i] = "dbname";
			values[i++] = cparams->override_dbname;
		}
		keywords[i] = "fallback_application_name";
		values[i++] = progname;
		keywords[i] = NULL;
		values[i++] = NULL;
		Assert(i <= lengthof(keywords));
		new_pass = false;
		AH->connection = PQconnectdbParams(keywords, values, true);
		if (!AH->connection) {
			
			pg_fatal("could not connect to database");
		}
		if (PQstatus(AH->connection) == CONNECTION_BAD &&
			PQconnectionNeedsPassword(AH->connection) &&
			password == NULL &&
			prompt_password != TRI_NO)
		{
			
			PQfinish(AH->connection);
			password = simple_prompt("Password: ", false);
			new_pass = true;
		}
	} while (new_pass);

	/* check to see that the backend connection was successfully made */
	if (PQstatus(AH->connection) == CONNECTION_BAD)
	{
		
		if (isReconnect)
			pg_fatal("reconnection failed: %s",
					 PQerrorMessage(AH->connection));
		else
			pg_fatal("%s",
					 PQerrorMessage(AH->connection));
	}

	//PGresult* res = 
	/* Start strict; later phases may override this. */
	PQclear(ExecuteSqlQueryForSingleRow((Archive *) AH,
										ALWAYS_SECURE_SEARCH_PATH_SQL));

	if (password && password != AH->savedPassword)
		free(password);

	/*
	 * We want to remember connection's actual password, whether or not we got
	 * it by prompting.  So we don't just store the password variable.
	 */
	if (PQconnectionUsedPassword(AH->connection))
	{
		free(AH->savedPassword);
		AH->savedPassword = pg_strdup(PQpass(AH->connection));
	}

	/* check for version mismatch */
	_check_database_version(AH);

	PQsetNoticeProcessor(AH->connection, notice_processor, NULL);
	
	/* arrange for SIGINT to issue a query cancel on this connection */
}




PGconn *
GetConnection(Archive *AHX)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;
	return AH->connection;
}


static void
die_on_query_failure(ArchiveHandle *AH, const char *query)
{
	pg_log_error("query failed: %s",
				 PQerrorMessage(AH->connection));
	pg_log_error_detail("Query was: %s", query);
	exit(1);
}


void
ExecuteSqlStatement(Archive *AHX, const char *query)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;
	PGresult   *res;

	res = PQexec(AH->connection, query);
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
		die_on_query_failure(AH, query);
	PQclear(res);
}

PGresult *
ExecuteSqlQuery(Archive *AHX, const char *query, ExecStatusType status)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;
	PGresult   *res;

	res = PQexec(AH->connection, query);
	if (PQresultStatus(res) != status)
		die_on_query_failure(AH, query);
	return res;
}

/*
 * Execute an SQL query and verify that we got exactly one row back.
 */
PGresult *
ExecuteSqlQueryForSingleRow(Archive *fout, const char *query)
{
	PGresult   *res;
	int			ntups;

	res = ExecuteSqlQuery(fout, query, PGRES_TUPLES_OK);

	/* Expecting a single result only */
	ntups = PQntuples(res);
	if (ntups != 1)
		pg_fatal(ngettext("query returned %d row instead of one: %s",
						  "query returned %d rows instead of one: %s",
						  ntups),
				 ntups, query);

	return res;
}

/*
 * Convenience function to send a query.
 * Monitors result to detect COPY statements
 */
void
ExecuteSqlCommand(ArchiveHandle *AH, const char *qry, const char *desc)
{
	PGconn	   *conn = AH->connection;
	PGresult   *res;

#ifdef NOT_USED
	fprintf(stderr, "Executing: '%s'\n\n", qry);
#endif
	res = PQexec(conn, qry);

	switch (PQresultStatus(res))
	{
		case PGRES_COMMAND_OK:
		case PGRES_TUPLES_OK:
		case PGRES_EMPTY_QUERY:
			/* A-OK */
			break;
		case PGRES_COPY_IN:
			/* Assume this is an expected result */
			AH->pgCopyIn = true;
			break;
		default:
			//exit(1);
			/* trouble */
			warn_or_exit_horribly(AH, "%s: %sCommand was: %s",
								  desc, PQerrorMessage(conn), qry);
			break;
	}

	PQclear(res);
}


void
quoteAclUserName(PQExpBuffer output, const char *input)
{
	const char *src;
	bool		safe = true;

	for (src = input; *src; src++)
	{
		/* This test had better match what putid() does */
		if (!isalnum((unsigned char) *src) && *src != '_')
		{
			safe = false;
			break;
		}
	}
	if (!safe)
		appendPQExpBufferChar(output, '"');
	for (src = input; *src; src++)
	{
		/* A double quote character in a username is encoded as "" */
		if (*src == '"')
			appendPQExpBufferChar(output, '"');
		appendPQExpBufferChar(output, *src);
	}
	if (!safe)
		appendPQExpBufferChar(output, '"');
}


NamespaceInfo *
findNamespaceByOid(Oid oid)
{
	CatalogId	catId;
	DumpableObject *dobj;

	catId.tableoid = NamespaceRelationId;
	catId.oid = oid;
	dobj = findObjectByCatalogId(catId);
	Assert(dobj == NULL || dobj->objType == DO_NAMESPACE);
	return (NamespaceInfo *) dobj;
}


/*
 * findExtensionByOid
 *	  finds the DumpableObject for the extension with the given oid
 *	  returns NULL if not found
 */
ExtensionInfo *
findExtensionByOid(Oid oid)
{
	CatalogId	catId;
	DumpableObject *dobj;

	catId.tableoid = ExtensionRelationId;
	catId.oid = oid;
	dobj = findObjectByCatalogId(catId);
	Assert(dobj == NULL || dobj->objType == DO_EXTENSION);
	return (ExtensionInfo *) dobj;
}


void
parseOidArray(const char *str, Oid *array, int arraysize)
{
	int			j,
				argNum;
	char		temp[100];
	char		s;

	argNum = 0;
	j = 0;
	for (;;)
	{
		s = *str++;
		if (s == ' ' || s == '\0')
		{
			if (j > 0)
			{
				if (argNum >= arraysize)
					pg_fatal("could not parse numeric array \"%s\": too many numbers", str);
				temp[j] = '\0';
				array[argNum++] = atooid(temp);
				j = 0;
			}
			if (s == '\0')
				break;
		}
		else
		{
			if (!(isdigit((unsigned char) s) || s == '-') ||
				j >= sizeof(temp) - 1)
				pg_fatal("could not parse numeric array \"%s\": invalid character in number", str);
			temp[j++] = s;
		}
	}

	while (argNum < arraysize)
		array[argNum++] = InvalidOid;
}


void
addObjectDependency(DumpableObject *dobj, DumpId refId)
{
	if (dobj->nDeps >= dobj->allocDeps)
	{
		if (dobj->allocDeps <= 0)
		{
			dobj->allocDeps = 16;
			dobj->dependencies = pg_malloc_array(DumpId, dobj->allocDeps);
		}
		else
		{
			dobj->allocDeps *= 2;
			dobj->dependencies = pg_realloc_array(dobj->dependencies,
												  DumpId, dobj->allocDeps);
		}
	}
	dobj->dependencies[dobj->nDeps++] = refId;
}


TypeInfo *
findTypeByOid(Oid oid)
{
	CatalogId	catId;
	DumpableObject *dobj;

	catId.tableoid = TypeRelationId;
	catId.oid = oid;
	dobj = findObjectByCatalogId(catId);
	Assert(dobj == NULL ||
		   dobj->objType == DO_TYPE || dobj->objType == DO_DUMMY_TYPE);
	return (TypeInfo *) dobj;
}


void
makeTableDataInfo(DumpOptions *dopt, TableInfo *tbinfo)
{
	TableDataInfo *tdinfo;

	/*
	 * Nothing to do if we already decided to dump the table.  This will
	 * happen for "config" tables.
	 */
	if (tbinfo->dataObj != NULL)
		return;

	/* Skip VIEWs (no data to dump) */
	if (tbinfo->relkind == RELKIND_VIEW)
		return;
	/* Skip FOREIGN TABLEs (no data to dump) unless requested explicitly */
	
	/* Skip partitioned tables (data in partitions) */
	if (tbinfo->relkind == RELKIND_PARTITIONED_TABLE)
		return;

	/* Don't dump data in unlogged tables, if so requested */
	if (tbinfo->relpersistence == RELPERSISTENCE_UNLOGGED &&
		dopt->no_unlogged_table_data)
		return;

	

	/* OK, let's dump it */
	tdinfo = (TableDataInfo *) pg_malloc(sizeof(TableDataInfo));

	if (tbinfo->relkind == RELKIND_MATVIEW)
		tdinfo->dobj.objType = DO_REFRESH_MATVIEW;
	else if (tbinfo->relkind == RELKIND_SEQUENCE)
		tdinfo->dobj.objType = DO_SEQUENCE_SET;
	else
		tdinfo->dobj.objType = DO_TABLE_DATA;

	/*
	 * Note: use tableoid 0 so that this object won't be mistaken for
	 * something that pg_depend entries apply to.
	 */
	tdinfo->dobj.catId.tableoid = 0;
	tdinfo->dobj.catId.oid = tbinfo->dobj.catId.oid;
	AssignDumpId(&tdinfo->dobj);
	tdinfo->dobj.name = tbinfo->dobj.name;
	tdinfo->dobj.namespace = tbinfo->dobj.namespace;
	tdinfo->tdtable = tbinfo;
	tdinfo->filtercond = NULL;	/* might get set later */
	addObjectDependency(&tdinfo->dobj, tbinfo->dobj.dumpId);

	/* A TableDataInfo contains data, of course */
	tdinfo->dobj.components |= DUMP_COMPONENT_DATA;

	tbinfo->dataObj = tdinfo;

	/* Make sure that we'll collect per-column info for this table. */
	tbinfo->interesting = true;
}


IndxInfo *
findIndexByOid(Oid oid)
{
	CatalogId	catId;
	DumpableObject *dobj;

	catId.tableoid = RelationRelationId;
	catId.oid = oid;
	dobj = findObjectByCatalogId(catId);
	Assert(dobj == NULL || dobj->objType == DO_INDEX);
	return (IndxInfo *) dobj;
}


int
strInArray(const char *pattern, char **arr, int arr_size)
{
	int			i;

	for (i = 0; i < arr_size; i++)
	{
		if (strcmp(pattern, arr[i]) == 0)
			return i;
	}
	return -1;
}

PublicationInfo *
findPublicationByOid(Oid oid)
{
	CatalogId	catId;
	DumpableObject *dobj;

	catId.tableoid = PublicationRelationId;
	catId.oid = oid;
	dobj = findObjectByCatalogId(catId);
	Assert(dobj == NULL || dobj->objType == DO_PUBLICATION);
	return (PublicationInfo *) dobj;
}

/*
 * findSubscriptionByOid
 *	  finds the DumpableObject for the subscription with the given oid
 *	  returns NULL if not found
 */
SubscriptionInfo *
findSubscriptionByOid(Oid oid)
{
	CatalogId	catId;
	DumpableObject *dobj;

	catId.tableoid = SubscriptionRelationId;
	catId.oid = oid;
	dobj = findObjectByCatalogId(catId);
	Assert(dobj == NULL || dobj->objType == DO_SUBSCRIPTION);
	return (SubscriptionInfo *) dobj;
}


bool
is_superuser(Archive *fout)
{
	ArchiveHandle *AH = (ArchiveHandle *) fout;
	const char *val;

	val = PQparameterStatus(AH->connection, "is_superuser");

	if (val && strcmp(val, "on") == 0)
		return true;

	return false;
}

CollInfo *
findCollationByOid(Oid oid)
{
	CatalogId	catId;
	DumpableObject *dobj;

	catId.tableoid = CollationRelationId;
	catId.oid = oid;
	dobj = findObjectByCatalogId(catId);
	Assert(dobj == NULL || dobj->objType == DO_COLLATION);
	return (CollInfo *) dobj;
}

FuncInfo *
findFuncByOid(Oid oid)
{
	CatalogId	catId;
	DumpableObject *dobj;

	catId.tableoid = ProcedureRelationId;
	catId.oid = oid;
	dobj = findObjectByCatalogId(catId);
	Assert(dobj == NULL || dobj->objType == DO_FUNC);
	return (FuncInfo *) dobj;
}


bool
variable_is_guc_list_quote(const char *name)
{
	if (pg_strcasecmp(name, "local_preload_libraries") == 0 ||
		pg_strcasecmp(name, "search_path") == 0 ||
		pg_strcasecmp(name, "session_preload_libraries") == 0 ||
		pg_strcasecmp(name, "shared_preload_libraries") == 0 ||
		pg_strcasecmp(name, "temp_tablespaces") == 0 ||
		pg_strcasecmp(name, "unix_socket_directories") == 0)
		return true;
	else
		return false;
}



int
EndLO(Archive *AHX, Oid oid)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;

	if (AH->EndLOPtr)
		AH->EndLOPtr(AH, AH->currToc, oid);

	return 1;
}


int
StartLO(Archive *AHX, Oid oid)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;

	if (!AH->StartLOPtr)
		pg_fatal("large-object output not supported in chosen format");

	AH->StartLOPtr(AH, AH->currToc, oid);

	return 1;
}

bool
SplitGUCList(char *rawstring, char separator,
			 char ***namelist)
{
	char	   *nextp = rawstring;
	bool		done = false;
	char	  **nextptr;

	/*
	 * Since we disallow empty identifiers, this is a conservative
	 * overestimate of the number of pointers we could need.  Allow one for
	 * list terminator.
	 */
	*namelist = nextptr = (char **)
		pg_malloc((strlen(rawstring) / 2 + 2) * sizeof(char *));
	*nextptr = NULL;

	while (isspace((unsigned char) *nextp))
		nextp++;				/* skip leading whitespace */

	if (*nextp == '\0')
		return true;			/* allow empty string */

	/* At the top of the loop, we are at start of a new identifier. */
	do
	{
		char	   *curname;
		char	   *endp;

		if (*nextp == '"')
		{
			/* Quoted name --- collapse quote-quote pairs */
			curname = nextp + 1;
			for (;;)
			{
				endp = strchr(nextp + 1, '"');
				if (endp == NULL)
					return false;	/* mismatched quotes */
				if (endp[1] != '"')
					break;		/* found end of quoted name */
				/* Collapse adjacent quotes into one quote, and look again */
				memmove(endp, endp + 1, strlen(endp));
				nextp = endp;
			}
			/* endp now points at the terminating quote */
			nextp = endp + 1;
		}
		else
		{
			/* Unquoted name --- extends to separator or whitespace */
			curname = nextp;
			while (*nextp && *nextp != separator &&
				   !isspace((unsigned char) *nextp))
				nextp++;
			endp = nextp;
			if (curname == nextp)
				return false;	/* empty unquoted name not allowed */
		}

		while (isspace((unsigned char) *nextp))
			nextp++;			/* skip trailing whitespace */

		if (*nextp == separator)
		{
			nextp++;
			while (isspace((unsigned char) *nextp))
				nextp++;		/* skip leading whitespace for next */
			/* we expect another name, so done remains false */
		}
		else if (*nextp == '\0')
			done = true;
		else
			return false;		/* invalid syntax */

		/* Now safe to overwrite separator with a null */
		*endp = '\0';

		/*
		 * Finished isolating current name --- add it to output array
		 */
		*nextptr++ = curname;

		/* Loop back if we didn't reach end of string */
	} while (!done);

	*nextptr = NULL;
	return true;
}


OprInfo *
findOprByOid(Oid oid)
{
	CatalogId	catId;
	DumpableObject *dobj;

	catId.tableoid = OperatorRelationId;
	catId.oid = oid;
	dobj = findObjectByCatalogId(catId);
	Assert(dobj == NULL || dobj->objType == DO_OPERATOR);
	return (OprInfo *) dobj;
}


bool
buildACLCommands(const char *name, const char *subname, const char *nspname,
				 const char *type, const char *acls, const char *baseacls,
				 const char *owner, const char *prefix, int remoteVersion,
				 PQExpBuffer sql)
{
	bool		ok = true;
	char	  **aclitems = NULL;
	char	  **baseitems = NULL;
	char	  **grantitems = NULL;
	char	  **revokeitems = NULL;
	int			naclitems = 0;
	int			nbaseitems = 0;
	int			ngrantitems = 0;
	int			nrevokeitems = 0;
	int			i;
	PQExpBuffer grantee,
				grantor,
				privs,
				privswgo;
	PQExpBuffer firstsql,
				secondsql;

	/*
	 * If the acl was NULL (initial default state), we need do nothing.  Note
	 * that this is distinguishable from all-privileges-revoked, which will
	 * look like an empty array ("{}").
	 */
	if (acls == NULL || *acls == '\0')
		return true;			/* object has default permissions */

	/* treat empty-string owner same as NULL */
	if (owner && *owner == '\0')
		owner = NULL;

	/* Parse the acls array */
	if (!parsePGArray(acls, &aclitems, &naclitems))
	{
		free(aclitems);
		return false;
	}

	/* Parse the baseacls too */
	if (!parsePGArray(baseacls, &baseitems, &nbaseitems))
	{
		free(aclitems);
		free(baseitems);
		return false;
	}

	/*
	 * Compare the actual ACL with the base ACL, extracting the privileges
	 * that need to be granted (i.e., are in the actual ACL but not the base
	 * ACL) and the ones that need to be revoked (the reverse).  We use plain
	 * string comparisons to check for matches.  In principle that could be
	 * fooled by extraneous issues such as whitespace, but since all these
	 * strings are the work of aclitemout(), it should be OK in practice.
	 * Besides, a false mismatch will just cause the output to be a little
	 * more verbose than it really needed to be.
	 */
	grantitems = (char **) pg_malloc(naclitems * sizeof(char *));
	for (i = 0; i < naclitems; i++)
	{
		bool		found = false;

		for (int j = 0; j < nbaseitems; j++)
		{
			if (strcmp(aclitems[i], baseitems[j]) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found)
			grantitems[ngrantitems++] = aclitems[i];
	}
	revokeitems = (char **) pg_malloc(nbaseitems * sizeof(char *));
	for (i = 0; i < nbaseitems; i++)
	{
		bool		found = false;

		for (int j = 0; j < naclitems; j++)
		{
			if (strcmp(baseitems[i], aclitems[j]) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found)
			revokeitems[nrevokeitems++] = baseitems[i];
	}

	/* Prepare working buffers */
	grantee = createPQExpBuffer();
	grantor = createPQExpBuffer();
	privs = createPQExpBuffer();
	privswgo = createPQExpBuffer();

	/*
	 * At the end, these two will be pasted together to form the result.
	 */
	firstsql = createPQExpBuffer();
	secondsql = createPQExpBuffer();

	/*
	 * Build REVOKE statements for ACLs listed in revokeitems[].
	 */
	for (i = 0; i < nrevokeitems; i++)
	{
		if (!parseAclItem(revokeitems[i],
						  type, name, subname, remoteVersion,
						  grantee, grantor, privs, NULL))
		{
			ok = false;
			break;
		}

		if (privs->len > 0)
		{
			appendPQExpBuffer(firstsql, "%sREVOKE %s ON %s ",
							  prefix, privs->data, type);
			if (nspname && *nspname)
				appendPQExpBuffer(firstsql, "%s.", fmtId(nspname));
			if (name && *name)
				appendPQExpBuffer(firstsql, "%s ", name);
			appendPQExpBufferStr(firstsql, "FROM ");
			if (grantee->len == 0)
				appendPQExpBufferStr(firstsql, "PUBLIC;\n");
			else
				appendPQExpBuffer(firstsql, "%s;\n",
								  fmtId(grantee->data));
		}
	}

	/*
	 * At this point we have issued REVOKE statements for all initial and
	 * default privileges that are no longer present on the object, so we are
	 * almost ready to GRANT the privileges listed in grantitems[].
	 *
	 * We still need some hacking though to cover the case where new default
	 * public privileges are added in new versions: the REVOKE ALL will revoke
	 * them, leading to behavior different from what the old version had,
	 * which is generally not what's wanted.  So add back default privs if the
	 * source database is too old to have had that particular priv.  (As of
	 * right now, no such cases exist in supported versions.)
	 */

	/*
	 * Scan individual ACL items to be granted.
	 *
	 * The order in which privileges appear in the ACL string (the order they
	 * have been GRANT'd in, which the backend maintains) must be preserved to
	 * ensure that GRANTs WITH GRANT OPTION and subsequent GRANTs based on
	 * those are dumped in the correct order.  However, some old server
	 * versions will show grants to PUBLIC before the owner's own grants; for
	 * consistency's sake, force the owner's grants to be output first.
	 */
	for (i = 0; i < ngrantitems; i++)
	{
		if (parseAclItem(grantitems[i], type, name, subname, remoteVersion,
						 grantee, grantor, privs, privswgo))
		{
			/*
			 * If the grantor isn't the owner, we'll need to use SET SESSION
			 * AUTHORIZATION to become the grantor.  Issue the SET/RESET only
			 * if there's something useful to do.
			 */
			if (privs->len > 0 || privswgo->len > 0)
			{
				PQExpBuffer thissql;

				/* Set owner as grantor if that's not explicit in the ACL */
				if (grantor->len == 0 && owner)
					printfPQExpBuffer(grantor, "%s", owner);

				/* Make sure owner's own grants are output before others */
				if (owner &&
					strcmp(grantee->data, owner) == 0 &&
					strcmp(grantor->data, owner) == 0)
					thissql = firstsql;
				else
					thissql = secondsql;

				if (grantor->len > 0
					&& (!owner || strcmp(owner, grantor->data) != 0))
					appendPQExpBuffer(thissql, "SET SESSION AUTHORIZATION %s;\n",
									  fmtId(grantor->data));

				if (privs->len > 0)
				{
					appendPQExpBuffer(thissql, "%sGRANT %s ON %s ",
									  prefix, privs->data, type);
					if (nspname && *nspname)
						appendPQExpBuffer(thissql, "%s.", fmtId(nspname));
					if (name && *name)
						appendPQExpBuffer(thissql, "%s ", name);
					appendPQExpBufferStr(thissql, "TO ");
					if (grantee->len == 0)
						appendPQExpBufferStr(thissql, "PUBLIC;\n");
					else
						appendPQExpBuffer(thissql, "%s;\n", fmtId(grantee->data));
				}
				if (privswgo->len > 0)
				{
					appendPQExpBuffer(thissql, "%sGRANT %s ON %s ",
									  prefix, privswgo->data, type);
					if (nspname && *nspname)
						appendPQExpBuffer(thissql, "%s.", fmtId(nspname));
					if (name && *name)
						appendPQExpBuffer(thissql, "%s ", name);
					appendPQExpBufferStr(thissql, "TO ");
					if (grantee->len == 0)
						appendPQExpBufferStr(thissql, "PUBLIC");
					else
						appendPQExpBufferStr(thissql, fmtId(grantee->data));
					appendPQExpBufferStr(thissql, " WITH GRANT OPTION;\n");
				}

				if (grantor->len > 0
					&& (!owner || strcmp(owner, grantor->data) != 0))
					appendPQExpBufferStr(thissql, "RESET SESSION AUTHORIZATION;\n");
			}
		}
		else
		{
			/* parseAclItem failed, give up */
			ok = false;
			break;
		}
	}

	destroyPQExpBuffer(grantee);
	destroyPQExpBuffer(grantor);
	destroyPQExpBuffer(privs);
	destroyPQExpBuffer(privswgo);

	appendPQExpBuffer(sql, "%s%s", firstsql->data, secondsql->data);
	destroyPQExpBuffer(firstsql);
	destroyPQExpBuffer(secondsql);

	free(aclitems);
	free(baseitems);
	free(grantitems);
	free(revokeitems);

	return ok;
}

bool
buildDefaultACLCommands(const char *type, const char *nspname,
						const char *acls, const char *acldefault,
						const char *owner,
						int remoteVersion,
						PQExpBuffer sql)
{
	PQExpBuffer prefix;

	prefix = createPQExpBuffer();

	/*
	 * We incorporate the target role directly into the command, rather than
	 * playing around with SET ROLE or anything like that.  This is so that a
	 * permissions error leads to nothing happening, rather than changing
	 * default privileges for the wrong user.
	 */
	appendPQExpBuffer(prefix, "ALTER DEFAULT PRIVILEGES FOR ROLE %s ",
					  fmtId(owner));
	if (nspname)
		appendPQExpBuffer(prefix, "IN SCHEMA %s ", fmtId(nspname));

	/*
	 * There's no such thing as initprivs for a default ACL, so the base ACL
	 * is always just the object-type-specific default.
	 */
	if (!buildACLCommands("", NULL, NULL, type,
						  acls, acldefault, owner,
						  prefix->data, remoteVersion, sql))
	{
		destroyPQExpBuffer(prefix);
		return false;
	}

	destroyPQExpBuffer(prefix);

	return true;
}


static char *
dequoteAclUserName(PQExpBuffer output, char *input)
{
	resetPQExpBuffer(output);

	while (*input && *input != '=')
	{
		/*
		 * If user name isn't quoted, then just add it to the output buffer
		 */
		if (*input != '"')
			appendPQExpBufferChar(output, *input++);
		else
		{
			/* Otherwise, it's a quoted username */
			input++;
			/* Loop until we come across an unescaped quote */
			while (!(*input == '"' && *(input + 1) != '"'))
			{
				if (*input == '\0')
					return input;	/* really a syntax error... */

				/*
				 * Quoting convention is to escape " as "".  Keep this code in
				 * sync with putid() in backend's acl.c.
				 */
				if (*input == '"' && *(input + 1) == '"')
					input++;
				appendPQExpBufferChar(output, *input++);
			}
			input++;
		}
	}
	return input;
}

bool
parseAclItem(const char *item, const char *type,
			 const char *name, const char *subname, int remoteVersion,
			 PQExpBuffer grantee, PQExpBuffer grantor,
			 PQExpBuffer privs, PQExpBuffer privswgo)
{
	char	   *buf;
	bool		all_with_go = true;
	bool		all_without_go = true;
	char	   *eqpos;
	char	   *slpos;
	char	   *pos;

	buf = pg_strdup(item);

	/* user or group name is string up to = */
	eqpos = dequoteAclUserName(grantee, buf);
	if (*eqpos != '=')
	{
		pg_free(buf);
		return false;
	}

	/* grantor should appear after / */
	slpos = strchr(eqpos + 1, '/');
	if (slpos)
	{
		*slpos++ = '\0';
		slpos = dequoteAclUserName(grantor, slpos);
		if (*slpos != '\0')
		{
			pg_free(buf);
			return false;
		}
	}
	else
	{
		pg_free(buf);
		return false;
	}

	/* privilege codes */
#define CONVERT_PRIV(code, keywd) \
do { \
	if ((pos = strchr(eqpos + 1, code))) \
	{ \
		if (*(pos + 1) == '*' && privswgo != NULL) \
		{ \
			AddAcl(privswgo, keywd, subname); \
			all_without_go = false; \
		} \
		else \
		{ \
			AddAcl(privs, keywd, subname); \
			all_with_go = false; \
		} \
	} \
	else \
		all_with_go = all_without_go = false; \
} while (0)

	resetPQExpBuffer(privs);
	resetPQExpBuffer(privswgo);

	if (strcmp(type, "TABLE") == 0 || strcmp(type, "SEQUENCE") == 0 ||
		strcmp(type, "TABLES") == 0 || strcmp(type, "SEQUENCES") == 0)
	{
		CONVERT_PRIV('r', "SELECT");

		if (strcmp(type, "SEQUENCE") == 0 ||
			strcmp(type, "SEQUENCES") == 0)
			/* sequence only */
			CONVERT_PRIV('U', "USAGE");
		else
		{
			/* table only */
			CONVERT_PRIV('a', "INSERT");
			CONVERT_PRIV('x', "REFERENCES");
			/* rest are not applicable to columns */
			if (subname == NULL)
			{
				CONVERT_PRIV('d', "DELETE");
				CONVERT_PRIV('t', "TRIGGER");
				CONVERT_PRIV('D', "TRUNCATE");
				CONVERT_PRIV('m', "MAINTAIN");
			}
		}

		/* UPDATE */
		CONVERT_PRIV('w', "UPDATE");
	}
	else if (strcmp(type, "FUNCTION") == 0 ||
			 strcmp(type, "FUNCTIONS") == 0)
		CONVERT_PRIV('X', "EXECUTE");
	else if (strcmp(type, "PROCEDURE") == 0 ||
			 strcmp(type, "PROCEDURES") == 0)
		CONVERT_PRIV('X', "EXECUTE");
	else if (strcmp(type, "LANGUAGE") == 0)
		CONVERT_PRIV('U', "USAGE");
	else if (strcmp(type, "SCHEMA") == 0 ||
			 strcmp(type, "SCHEMAS") == 0)
	{
		CONVERT_PRIV('C', "CREATE");
		CONVERT_PRIV('U', "USAGE");
	}
	else if (strcmp(type, "DATABASE") == 0)
	{
		CONVERT_PRIV('C', "CREATE");
		CONVERT_PRIV('c', "CONNECT");
		CONVERT_PRIV('T', "TEMPORARY");
	}
	else if (strcmp(type, "TABLESPACE") == 0)
		CONVERT_PRIV('C', "CREATE");
	else if (strcmp(type, "TYPE") == 0 ||
			 strcmp(type, "TYPES") == 0)
		CONVERT_PRIV('U', "USAGE");
	else if (strcmp(type, "FOREIGN DATA WRAPPER") == 0)
		CONVERT_PRIV('U', "USAGE");
	else if (strcmp(type, "FOREIGN SERVER") == 0)
		CONVERT_PRIV('U', "USAGE");
	else if (strcmp(type, "FOREIGN TABLE") == 0)
		CONVERT_PRIV('r', "SELECT");
	else if (strcmp(type, "PARAMETER") == 0)
	{
		CONVERT_PRIV('s', "SET");
		CONVERT_PRIV('A', "ALTER SYSTEM");
	}
	else if (strcmp(type, "LARGE OBJECT") == 0)
	{
		CONVERT_PRIV('r', "SELECT");
		CONVERT_PRIV('w', "UPDATE");
	}
	else
		abort();

#undef CONVERT_PRIV

	if (all_with_go)
	{
		resetPQExpBuffer(privs);
		printfPQExpBuffer(privswgo, "ALL");
		if (subname)
			appendPQExpBuffer(privswgo, "(%s)", subname);
	}
	else if (all_without_go)
	{
		resetPQExpBuffer(privswgo);
		printfPQExpBuffer(privs, "ALL");
		if (subname)
			appendPQExpBuffer(privs, "(%s)", subname);
	}

	pg_free(buf);

	return true;
}


void
WriteData(Archive *AHX, const void *data, size_t dLen)
{
	ArchiveHandle *AH = (ArchiveHandle *) AHX;

	if (!AH->currToc)
		pg_fatal("internal error -- WriteData cannot be called outside the context of a DataDumper routine");

	AH->WriteDataPtr(AH, data, dLen);
}

RestoreOptions *
NewRestoreOptions(void)
{
	RestoreOptions *opts;

	opts = (RestoreOptions *) pg_malloc0(sizeof(RestoreOptions));

	/* set any fields that shouldn't default to zeroes */
	opts->format = archUnknown;
	opts->cparams.promptPassword = TRI_DEFAULT;
	opts->dumpSections = DUMP_UNSECTIONED;
	opts->compression_spec.algorithm = PG_COMPRESSION_NONE;
	opts->compression_spec.level = 0;

	return opts;
}



void
InitCompressFileHandleNone(CompressFileHandle *CFH,
						   const pg_compress_specification compression_spec)
{
	CFH->open_func = open_none;
	CFH->open_write_func = open_write_none;
	CFH->read_func = read_none;
	CFH->write_func = write_none;
	CFH->gets_func = gets_none;
	CFH->getc_func = getc_none;
	CFH->close_func = close_none;
	CFH->eof_func = eof_none;
	CFH->get_error_func = get_error_none;

	CFH->private_data = NULL;
}
