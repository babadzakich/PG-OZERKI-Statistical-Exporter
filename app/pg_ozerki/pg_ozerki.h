

#include <stdbool.h>
#include "postgres_ext.h"
#include "postgres_fe.h"
#include "catalog/pg_aggregate_d.h"
#include "catalog/pg_am_d.h"
#include "catalog/pg_attribute_d.h"
#include "catalog/pg_authid_d.h"
#include "catalog/pg_cast_d.h"
#include "catalog/pg_class_d.h"
#include "catalog/pg_default_acl_d.h"
#include "catalog/pg_largeobject_d.h"
#include "catalog/pg_largeobject_metadata_d.h"
#include "catalog/pg_proc_d.h"
#include "catalog/pg_subscription.h"
#include "catalog/pg_trigger_d.h"
#include "catalog/pg_type_d.h"
#include "catalog/pg_extension_d.h"
#include "catalog/pg_publication_d.h"
#include "catalog/pg_namespace_d.h"
#include "catalog/pg_operator_d.h"
#include "common/compression.h"
#include "common/file_utils.h"
#include "fe_utils/simple_list.h"
#include "fe_utils/print.h"
#include "libpq-fe.h"
#include "libpq/libpq-fs.h"
#include "pqexpbuffer.h"
#include "pg_config.h"
#include "c.h"
#include <time.h>
#include <unistd.h>
#include <ctype.h>

#include "lib/binaryheap.h"
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#include "common/connect.h"
#include "common/string.h"
#include "common/int.h"
#include "common/logging.h"
#include "fe_utils/string_utils.h"
#include <stdint.h>

#define oidcmp(x,y) ( ((x) < (y) ? -1 : ((x) > (y)) ?  1 : 0) )

#define appendStringLiteralAH(buf,str,AH) \
	appendStringLiteral(buf, str, (AH)->encoding, (AH)->std_strings)

#define DUMP_COMPONENT_NONE			(0)
#define DUMP_COMPONENT_DEFINITION	(1 << 0)
#define DUMP_COMPONENT_DATA			(1 << 1)
#define DUMP_COMPONENT_COMMENT		(1 << 2)
#define DUMP_COMPONENT_SECLABEL		(1 << 3)
#define DUMP_COMPONENT_ACL			(1 << 4)
#define DUMP_COMPONENT_POLICY		(1 << 5)
#define DUMP_COMPONENT_USERMAP		(1 << 6)
#define DUMP_COMPONENT_ALL			(0xFFFF)


#define CollationRelationId 3456
#define CollationNameEncNspIndexId 3164
#define CollationOidIndexId 3085

#define fmtQualifiedDumpable(obj) \
	fmtQualifiedId((obj)->dobj.namespace->dobj.name, \
				   (obj)->dobj.name)


#define DUMP_COMPONENTS_REQUIRING_LOCK (\
		DUMP_COMPONENT_DEFINITION |\
		DUMP_COMPONENT_DATA |\
		DUMP_COMPONENT_POLICY)


#define		  ATTRIBUTE_IDENTITY_ALWAYS		'a'


typedef enum
{
	RESTORE_PASS_MAIN = 0,		/* Main pass (most TOC item types) */
	RESTORE_PASS_ACL,			/* ACL item types */
	RESTORE_PASS_POST_ACL,		/* Event trigger and matview refresh items */

#define RESTORE_PASS_LAST RESTORE_PASS_POST_ACL
} RestorePass;

#ifndef WIN32
#define PGDUMP_STRFTIME_FMT  "%Y-%m-%d %H:%M:%S %Z"
#else
#define PGDUMP_STRFTIME_FMT  "%Y-%m-%d %H:%M:%S"
#endif

#define K_VERS_1_0	MAKE_ARCHIVE_VERSION(1, 0, 0)
#define K_VERS_1_2	MAKE_ARCHIVE_VERSION(1, 2, 0)	/* Allow No ZLIB */
#define K_VERS_1_3	MAKE_ARCHIVE_VERSION(1, 3, 0)	/* BLOBS */
#define K_VERS_1_4	MAKE_ARCHIVE_VERSION(1, 4, 0)	/* Date & name in header */
#define K_VERS_1_5	MAKE_ARCHIVE_VERSION(1, 5, 0)	/* Handle dependencies */
#define K_VERS_1_6	MAKE_ARCHIVE_VERSION(1, 6, 0)	/* Schema field in TOCs */
#define K_VERS_1_7	MAKE_ARCHIVE_VERSION(1, 7, 0)	/* File Offset size in
													 * header */
#define K_VERS_1_8	MAKE_ARCHIVE_VERSION(1, 8, 0)	/* change interpretation
													 * of ID numbers and
													 * dependencies */
#define K_VERS_1_9	MAKE_ARCHIVE_VERSION(1, 9, 0)	/* add default_with_oids
													 * tracking */
#define K_VERS_1_10 MAKE_ARCHIVE_VERSION(1, 10, 0)	/* add tablespace */
#define K_VERS_1_11 MAKE_ARCHIVE_VERSION(1, 11, 0)	/* add toc section
													 * indicator */
#define K_VERS_1_12 MAKE_ARCHIVE_VERSION(1, 12, 0)	/* add separate BLOB
													 * entries */
#define K_VERS_1_13 MAKE_ARCHIVE_VERSION(1, 13, 0)	/* change search_path
													 * behavior */
#define K_VERS_1_14 MAKE_ARCHIVE_VERSION(1, 14, 0)	/* add tableam */
#define K_VERS_1_15 MAKE_ARCHIVE_VERSION(1, 15, 0)	/* add
													 * compression_algorithm
													 * in header */
#define K_VERS_1_16 MAKE_ARCHIVE_VERSION(1, 16, 0)	/* BLOB METADATA entries
													 * and multiple BLOBS,
													 * relkind */



#define K_VERS_MAJOR 1
#define K_VERS_MINOR 16
#define K_VERS_REV 0

#define MAKE_ARCHIVE_VERSION(major, minor, rev) (((major) * 256 + (minor)) * 256 + (rev))
#define K_VERS_SELF MAKE_ARCHIVE_VERSION(K_VERS_MAJOR, K_VERS_MINOR, K_VERS_REV)

#define DUMP_UNSECTIONED	0xff
#define LOBBUFSIZE 16384
#define InvalidAttrNumber		0
#define InvalidDumpId 0



#define SelfItemPointerAttributeNumber			(-1)
#define MinTransactionIdAttributeNumber			(-2)
#define MinCommandIdAttributeNumber				(-3)
#define MaxTransactionIdAttributeNumber			(-4)
#define MaxCommandIdAttributeNumber				(-5)
#define TableOidAttributeNumber					(-6)
#define FirstLowInvalidHeapAttributeNumber		(-7)



#define REQ_SCHEMA	0x01		/* want schema */
#define REQ_DATA	0x02		/* want data */
#define REQ_SPECIAL	0x04		/* for special TOC entries */


#define DUMP_PRE_DATA		0x01
#define DUMP_DATA			0x02
#define DUMP_POST_DATA		0x04
#define DUMP_UNSECTIONED	0xff



#define WORKER_OK					  0
#define WORKER_CREATE_DONE			  10
#define WORKER_INHIBIT_DATA			  11
#define WORKER_IGNORED_ERRORS		  12


extern const char *progname;


//extern Oid g_last_builtin_oid;

typedef uint32_t DumpComponents;
typedef int DumpId;

typedef struct _archiveHandle ArchiveHandle;
typedef struct _tocEntry TocEntry;


typedef enum trivalue
{
	TRI_DEFAULT,
	TRI_NO,
	TRI_YES,
} trivalue;

typedef enum OidOptions
{
	zeroIsError = 1,
	zeroAsStar = 2,
	zeroAsNone = 4,
} OidOptions;

typedef void (*ClosePtrType) (ArchiveHandle *AH);
typedef void (*ReopenPtrType) (ArchiveHandle *AH);
typedef void (*ArchiveEntryPtrType) (ArchiveHandle *AH, TocEntry *te);

typedef void (*StartDataPtrType) (ArchiveHandle *AH, TocEntry *te);
typedef void (*WriteDataPtrType) (ArchiveHandle *AH, const void *data, size_t dLen);
typedef void (*EndDataPtrType) (ArchiveHandle *AH, TocEntry *te);

typedef void (*StartLOsPtrType) (ArchiveHandle *AH, TocEntry *te);
typedef void (*StartLOPtrType) (ArchiveHandle *AH, TocEntry *te, Oid oid);
typedef void (*EndLOPtrType) (ArchiveHandle *AH, TocEntry *te, Oid oid);
typedef void (*EndLOsPtrType) (ArchiveHandle *AH, TocEntry *te);

typedef int (*WriteBytePtrType) (ArchiveHandle *AH, const int i);
typedef int (*ReadBytePtrType) (ArchiveHandle *AH);
typedef void (*WriteBufPtrType) (ArchiveHandle *AH, const void *c, size_t len);
typedef void (*ReadBufPtrType) (ArchiveHandle *AH, void *buf, size_t len);
typedef void (*WriteExtraTocPtrType) (ArchiveHandle *AH, TocEntry *te);
typedef void (*ReadExtraTocPtrType) (ArchiveHandle *AH, TocEntry *te);
typedef void (*PrintExtraTocPtrType) (ArchiveHandle *AH, TocEntry *te);
typedef void (*PrintTocDataPtrType) (ArchiveHandle *AH, TocEntry *te);



typedef void (*PrepParallelRestorePtrType) (ArchiveHandle *AH);
typedef void (*ClonePtrType) (ArchiveHandle *AH);
typedef void (*DeClonePtrType) (ArchiveHandle *AH);

typedef int (*WorkerJobDumpPtrType) (ArchiveHandle *AH, TocEntry *te);
typedef int (*WorkerJobRestorePtrType) (ArchiveHandle *AH, TocEntry *te);

typedef size_t (*CustomOutPtrType) (ArchiveHandle *AH, const void *buf, size_t len);
typedef enum _teSection
{
	SECTION_NONE = 1,			/* comments, ACLs, etc; can be anywhere */
	SECTION_PRE_DATA,			/* stuff to be processed before data */
	SECTION_DATA,				/* table data, large objects, LO comments */
	SECTION_POST_DATA,			/* stuff to be processed after data */
} teSection;


typedef enum _archiveMode
{
	archModeAppend,
	archModeWrite,
	archModeRead,
} ArchiveMode;

typedef struct
{
	/* Note: this struct must not contain any unused bytes */
	Oid			tableoid;
	Oid			oid;
} CatalogId;

extern const CatalogId nilCatalogId;



typedef enum
{
	/* When modifying this enum, update priority tables in pg_dump_sort.c! */
	DO_NAMESPACE,
	DO_EXTENSION,
	DO_TYPE,
	DO_SHELL_TYPE,
	DO_FUNC,
	DO_AGG,
	DO_OPERATOR,
	DO_ACCESS_METHOD,
	DO_OPCLASS,
	DO_OPFAMILY,
	DO_COLLATION,
	DO_CONVERSION,
	DO_TABLE,
	DO_TABLE_ATTACH,
	DO_ATTRDEF,
	DO_INDEX,
	DO_INDEX_ATTACH,
	DO_STATSEXT,
	DO_RULE,
	DO_TRIGGER,
	DO_CONSTRAINT,
	DO_FK_CONSTRAINT,			/* see note for ConstraintInfo */
	DO_PROCLANG,
	DO_CAST,
	DO_TABLE_DATA,
	DO_SEQUENCE_SET,
	DO_DUMMY_TYPE,
	DO_TSPARSER,
	DO_TSDICT,
	DO_TSTEMPLATE,
	DO_TSCONFIG,
	DO_FDW,
	DO_FOREIGN_SERVER,
	DO_DEFAULT_ACL,
	DO_TRANSFORM,
	DO_LARGE_OBJECT,
	DO_LARGE_OBJECT_DATA,
	DO_PRE_DATA_BOUNDARY,
	DO_POST_DATA_BOUNDARY,
	DO_EVENT_TRIGGER,
	DO_REFRESH_MATVIEW,
	DO_POLICY,
	DO_PUBLICATION,
	DO_PUBLICATION_REL,
	DO_PUBLICATION_TABLE_IN_SCHEMA,
	DO_SUBSCRIPTION,
	DO_SUBSCRIPTION_REL,		/* see note for SubRelInfo */
} DumpableObjectType;


typedef struct _dumpableObject
{
	DumpableObjectType objType;
	CatalogId	catId;			/* zero if not a cataloged object */
	DumpId		dumpId;			/* assigned by AssignDumpId() */
	char	   *name;			/* object name (should never be NULL) */
	struct _namespaceInfo *namespace;	/* containing namespace, or NULL */
	DumpComponents dump;		/* bitmask of components requested to dump */
	DumpComponents dump_contains;	/* as above, but for contained objects */
	DumpComponents components;	/* bitmask of components available to dump */
	bool		ext_member;		/* true if object is member of extension */
	bool		depends_on_ext; /* true if object depends on an extension */
	DumpId	   *dependencies;	/* dumpIds of objects this one depends on */
	int			nDeps;			/* number of valid dependencies */
	int			allocDeps;		/* allocated size of dependencies[] */
} DumpableObject;

typedef enum _archiveFormat
{
	archUnknown = 0,
	archCustom = 1,
	archTar = 3,
	archNull = 4,
	archDirectory = 5,
} ArchiveFormat;

typedef enum
{
	STAGE_NONE = 0,
	STAGE_INITIALIZING,
	STAGE_PROCESSING,
	STAGE_FINALIZING,
} ArchiverStage;

enum _dumpPreparedQueries
{
	PREPQUERY_DUMPAGG,
	PREPQUERY_DUMPBASETYPE,
	PREPQUERY_DUMPCOMPOSITETYPE,
	PREPQUERY_DUMPDOMAIN,
	PREPQUERY_DUMPENUMTYPE,
	PREPQUERY_DUMPFUNC,
	PREPQUERY_DUMPOPR,
	PREPQUERY_DUMPRANGETYPE,
	PREPQUERY_DUMPTABLEATTACH,
	PREPQUERY_GETCOLUMNACLS,
	PREPQUERY_GETDOMAINCONSTRAINTS,
	NUM_PREP_QUERIES			/* must be last */
};
typedef struct CompressFileHandle CompressFileHandle;

typedef struct
{
	Oid			roleoid;		/* role's OID */
	const char *rolename;		/* role's name */
} RoleNameItem;

typedef struct _dumpableAcl
{
	char	   *acl;			/* the object's actual ACL string */
	char	   *acldefault;		/* default ACL for the object's type & owner */
	/* these fields come from the object's pg_init_privs entry, if any: */
	char		privtype;		/* entry type, 'i' or 'e'; 0 if no entry */
	char	   *initprivs;		/* the object's initial ACL string, or NULL */
} DumpableAcl;

typedef struct _namespaceInfo
{
	DumpableObject dobj;
	DumpableAcl dacl;
	bool		create;			/* CREATE SCHEMA, or just set owner? */
	Oid			nspowner;		/* OID of owner */
	const char *rolname;		/* name of owner */
} NamespaceInfo;


typedef struct _typeInfo
{
	DumpableObject dobj;
	DumpableAcl dacl;

	/*
	 * Note: dobj.name is the raw pg_type.typname entry.  ftypname is the
	 * result of format_type(), which will be quoted if needed, and might be
	 * schema-qualified too.
	 */
	char	   *ftypname;
	const char *rolname;
	Oid			typelem;
	Oid			typrelid;
	char		typrelkind;		/* 'r', 'v', 'c', etc */
	char		typtype;		/* 'b', 'c', etc */
	bool		isArray;		/* true if auto-generated array type */
	bool		isMultirange;	/* true if auto-generated multirange type */
	bool		isDefined;		/* true if typisdefined */
	/* If needed, we'll create a "shell type" entry for it; link that here: */
	struct _shellTypeInfo *shellType;	/* shell-type entry, or NULL */
	/* If it's a domain, we store links to its constraints here: */
	int			nDomChecks;
	struct _constraintInfo *domChecks;
} TypeInfo;

typedef struct _shellTypeInfo
{
	DumpableObject dobj;

	TypeInfo   *baseType;		/* back link to associated base type */
} ShellTypeInfo;

typedef struct _funcInfo
{
	DumpableObject dobj;
	DumpableAcl dacl;
	const char *rolname;
	Oid			lang;
	int			nargs;
	Oid		   *argtypes;
	Oid			prorettype;
	bool		postponed_def;	/* function must be postponed into post-data */
} FuncInfo;

/* AggInfo is a superset of FuncInfo */
typedef struct _aggInfo
{
	FuncInfo	aggfn;
	/* we don't require any other fields at the moment */
} AggInfo;

typedef struct _oprInfo
{
	DumpableObject dobj;
	const char *rolname;
	char		oprkind;
	Oid			oprcode;
} OprInfo;

typedef struct _dumpableObjectWithAcl
{
	DumpableObject dobj;
	DumpableAcl dacl;
} DumpableObjectWithAcl;










typedef struct _accessMethodInfo
{
	DumpableObject dobj;
	char		amtype;
	char	   *amhandler;
} AccessMethodInfo;

typedef struct _opclassInfo
{
	DumpableObject dobj;
	const char *rolname;
} OpclassInfo;

typedef struct _opfamilyInfo
{
	DumpableObject dobj;
	const char *rolname;
} OpfamilyInfo;

typedef struct _collInfo
{
	DumpableObject dobj;
	const char *rolname;
} CollInfo;

typedef struct _convInfo
{
	DumpableObject dobj;
	const char *rolname;
} ConvInfo;


struct CompressFileHandle
{
	/*
	 * Open a file in mode.
	 *
	 * Pass either 'path' or 'fd' depending on whether a file path or a file
	 * descriptor is available. 'mode' can be one of 'r', 'rb', 'w', 'wb',
	 * 'a', and 'ab'. Requires an already initialized CompressFileHandle.
	 *
	 * Returns true on success and false on error.
	 */
	bool		(*open_func) (const char *path, int fd, const char *mode,
							  CompressFileHandle *CFH);

	/*
	 * Open a file for writing.
	 *
	 * 'mode' can be one of 'w', 'wb', 'a', and 'ab'. Requires an already
	 * initialized CompressFileHandle.
	 *
	 * Returns true on success and false on error.
	 */
	bool		(*open_write_func) (const char *path, const char *mode,
									CompressFileHandle *CFH);

	/*
	 * Read 'size' bytes of data from the file and store them into 'ptr'.
	 * Optionally it will store the number of bytes read in 'rsize'.
	 *
	 * Returns true on success and throws an internal error otherwise.
	 */
	bool		(*read_func) (void *ptr, size_t size, size_t *rsize,
							  CompressFileHandle *CFH);

	/*
	 * Write 'size' bytes of data into the file from 'ptr'.
	 *
	 * Returns true on success and false on error.
	 */
	bool		(*write_func) (const void *ptr, size_t size,
							   struct CompressFileHandle *CFH);

	/*
	 * Read at most size - 1 characters from the compress file handle into
	 * 's'.
	 *
	 * Stop if an EOF or a newline is found first. 's' is always null
	 * terminated and contains the newline if it was found.
	 *
	 * Returns 's' on success, and NULL on error or when end of file occurs
	 * while no characters have been read.
	 */
	char	   *(*gets_func) (char *s, int size, CompressFileHandle *CFH);

	/*
	 * Read the next character from the compress file handle as 'unsigned
	 * char' cast into 'int'.
	 *
	 * Returns the character read on success and throws an internal error
	 * otherwise. It treats EOF as error.
	 */
	int			(*getc_func) (CompressFileHandle *CFH);

	/*
	 * Test if EOF is reached in the compress file handle.
	 *
	 * Returns true if it is reached.
	 */
	bool		(*eof_func) (CompressFileHandle *CFH);

	/*
	 * Close an open file handle.
	 *
	 * Returns true on success and false on error.
	 */
	bool		(*close_func) (CompressFileHandle *CFH);

	/*
	 * Get a pointer to a string that describes an error that occurred during
	 * a compress file handle operation.
	 */
	const char *(*get_error_func) (CompressFileHandle *CFH);

	/*
	 * Compression specification for this file handle.
	 */
	pg_compress_specification compression_spec;

	/*
	 * Private data to be used by the compressor.
	 */
	void	   *private_data;
};

typedef enum
{
	SQL_SCAN = 0,				/* normal */
	SQL_IN_SINGLE_QUOTE,		/* '...' literal */
	SQL_IN_DOUBLE_QUOTE,		/* "..." identifier */
} sqlparseState;

typedef enum
{
	OUTPUT_SQLCMDS = 0,			/* emitting general SQL commands */
	OUTPUT_COPYDATA,			/* writing COPY data */
	OUTPUT_OTHERDATA,			/* writing data as INSERT commands */
} ArchiverOutput;

typedef struct
{
	sqlparseState state;		/* see above */
	bool		backSlash;		/* next char is backslash quoted? */
	PQExpBuffer curCmd;			/* incomplete line (NULL if not created) */
} sqlparseInfo;

typedef struct _connParams
{
	/* These fields record the actual command line parameters */
	char	   *dbname;			/* this may be a connstring! */
	char	   *pgport;
	char	   *pghost;
	char	   *username;
	trivalue	promptPassword;
	/* If not NULL, this overrides the dbname obtained from command line */
	/* (but *only* the DB name, not anything else in the connstring) */
	char	   *override_dbname;
} ConnParams;

typedef struct _restoreOptions
{
	int			createDB;		/* Issue commands to create the database */
	int			noOwner;		/* Don't try to match original object owner */
	int			noTableAm;		/* Don't issue table-AM-related commands */
	int			noTablespace;	/* Don't issue tablespace-related commands */
	int			disable_triggers;	/* disable triggers during data-only
									 * restore */
	int			use_setsessauth;	/* Use SET SESSION AUTHORIZATION commands
									 * instead of OWNER TO */
	char	   *superuser;		/* Username to use as superuser */
	char	   *use_role;		/* Issue SET ROLE to this */
	int			dropSchema;
	int			disable_dollar_quoting;
	int			dump_inserts;	/* 0 = COPY, otherwise rows per INSERT */
	int			column_inserts;
	int			if_exists;
	int			no_comments;	/* Skip comments */
	int			no_publications;	/* Skip publication entries */
	int			no_security_labels; /* Skip security label entries */
	int			no_subscriptions;	/* Skip subscription entries */
	int			strict_names;

	const char *filename;
	int			dataOnly;
	int			schemaOnly;
	int			dumpSections;
	int			verbose;
	int			aclsSkip;
	const char *lockWaitTimeout;
	int			include_everything;

	int			tocSummary;
	char	   *tocFile;
	int			format;
	char	   *formatName;

	int			selTypes;
	int			selIndex;
	int			selFunction;
	int			selTrigger;
	int			selTable;
	SimpleStringList indexNames;
	SimpleStringList functionNames;
	SimpleStringList schemaNames;
	SimpleStringList schemaExcludeNames;
	SimpleStringList triggerNames;
	SimpleStringList tableNames;

	int			useDB;
	ConnParams	cparams;		/* parameters to use if useDB */

	int			noDataForFailedTables;
	int			exit_on_error;
	pg_compress_specification compression_spec; /* Specification for
												 * compression */
	int			suppressDumpWarnings;	/* Suppress output of WARNING entries
										 * to stderr */

	bool		single_txn;		/* restore all TOCs in one transaction */
	int			txn_size;		/* restore this many TOCs per txn, if > 0 */

	bool	   *idWanted;		/* array showing which dump IDs to emit */
	int			enable_row_security;
	int			sequence_data;	/* dump sequence data even in schema-only mode */
	int			binary_upgrade;
} RestoreOptions;

typedef struct _dumpOptions
{
	ConnParams	cparams;

	int			binary_upgrade;

	/* various user-settable parameters */
	bool		schemaOnly;
	bool		dataOnly;
	int			dumpSections;	/* bitmask of chosen sections */
	bool		aclsSkip;
	const char *lockWaitTimeout;
	int			dump_inserts;	/* 0 = COPY, otherwise rows per INSERT */

	/* flags for various command-line long options */
	int			disable_dollar_quoting;
	int			column_inserts;
	int			if_exists;
	int			no_comments;
	int			no_security_labels;
	int			no_publications;
	int			no_subscriptions;
	int			no_toast_compression;
	int			no_unlogged_table_data;
	int			serializable_deferrable;
	int			disable_triggers;
	int			outputNoTableAm;
	int			outputNoTablespaces;
	int			use_setsessauth;
	int			enable_row_security;
	int			load_via_partition_root;

	/* default, if no "inclusion" switches appear, is to dump everything */
	bool		include_everything;

	int			outputClean;
	int			outputCreateDB;
	bool		outputLOs;
	bool		dontOutputLOs;
	int			outputNoOwner;
	char	   *outputSuperuser;

	int			sequence_data;	/* dump sequence data even in schema-only mode */
	int			do_nothing;
} DumpOptions;



typedef struct Archive
{
	DumpOptions *dopt;			/* options, if dumping */
	RestoreOptions *ropt;		/* options, if restoring */

	int			verbose;
	char	   *remoteVersionStr;	/* server's version string */
	int			remoteVersion;	/* same in numeric form */
	bool		isStandby;		/* is server a standby node */

	int			minRemoteVersion;	/* allowable range */
	int			maxRemoteVersion;

	int			numWorkers;		/* number of parallel processes */
	char	   *sync_snapshot_id;	/* sync snapshot id for parallel operation */

	/* info needed for string escaping */
	int			encoding;		/* libpq code for client_encoding */
	bool		std_strings;	/* standard_conforming_strings */

	/* other important stuff */
	char	   *searchpath;		/* search_path to set during restore */
	char	   *use_role;		/* Issue SET ROLE to this */

	/* error handling */
	bool		exit_on_error;	/* whether to exit on SQL errors... */
	int			n_errors;		/* number of errors (if no die) */

	/* prepared-query status */
	bool	   *is_prepared;	/* indexed by enum _dumpPreparedQueries */

	/* The rest is private */
} Archive;

typedef void (*SetupWorkerPtrType) (Archive *AH);

typedef int (*DataDumperPtr) (Archive *AH, const void *userArg);

struct _archiveHandle
{
	Archive		public;			/* Public part of archive */
	int			version;		/* Version of file */

	char	   *archiveRemoteVersion;	/* When reading an archive, the
										 * version of the dumped DB */
	char	   *archiveDumpVersion; /* When reading an archive, the version of
									 * the dumper */

	size_t		intSize;		/* Size of an integer in the archive */
	size_t		offSize;		/* Size of a file offset in the archive -
								 * Added V1.7 */
	sqlparseInfo sqlparse;		/* state for parsing INSERT data */

	time_t		createDate;		/* Date archive created */

	/*
	 * Fields used when discovering archive format.  For tar format, we load
	 * the first block into the lookahead buffer, and verify that it looks
	 * like a tar header.  The tar module must then consume bytes from the
	 * lookahead buffer before reading any more from the file.  For custom
	 * format, we load only the "PGDMP" marker into the buffer, and then set
	 * readHeader after confirming it matches.  The buffer is vestigial in
	 * this case, as the subsequent code just checks readHeader and doesn't
	 * examine the buffer.
	 */
	int			readHeader;		/* Set if we already read "PGDMP" marker */
	char	   *lookahead;		/* Buffer used when reading header to discover
								 * format */
	size_t		lookaheadSize;	/* Allocated size of buffer */
	size_t		lookaheadLen;	/* Length of valid data in lookahead */
	size_t		lookaheadPos;	/* Current read position in lookahead buffer */

	ArchiveEntryPtrType ArchiveEntryPtr;	/* Called for each metadata object */
	StartDataPtrType StartDataPtr;	/* Called when table data is about to be
									 * dumped */
	WriteDataPtrType WriteDataPtr;	/* Called to send some table data to the
									 * archive */
	EndDataPtrType EndDataPtr;	/* Called when table data dump is finished */
	WriteBytePtrType WriteBytePtr;	/* Write a byte to output */
	ReadBytePtrType ReadBytePtr;	/* Read a byte from an archive */
	WriteBufPtrType WriteBufPtr;	/* Write a buffer of output to the archive */
	ReadBufPtrType ReadBufPtr;	/* Read a buffer of input from the archive */
	ClosePtrType ClosePtr;		/* Close the archive */
	ReopenPtrType ReopenPtr;	/* Reopen the archive */
	WriteExtraTocPtrType WriteExtraTocPtr;	/* Write extra TOC entry data
											 * associated with the current
											 * archive format */
	ReadExtraTocPtrType ReadExtraTocPtr;	/* Read extra info associated with
											 * archive format */
	PrintExtraTocPtrType PrintExtraTocPtr;	/* Extra TOC info for format */
	PrintTocDataPtrType PrintTocDataPtr;

	StartLOsPtrType StartLOsPtr;
	EndLOsPtrType EndLOsPtr;
	StartLOPtrType StartLOPtr;
	EndLOPtrType EndLOPtr;

	SetupWorkerPtrType SetupWorkerPtr;
	WorkerJobDumpPtrType WorkerJobDumpPtr;
	WorkerJobRestorePtrType WorkerJobRestorePtr;

	PrepParallelRestorePtrType PrepParallelRestorePtr;
	ClonePtrType ClonePtr;		/* Clone format-specific fields */
	DeClonePtrType DeClonePtr;	/* Clean up cloned fields */

	CustomOutPtrType CustomOutPtr;	/* Alternative script output routine */

	/* Stuff for direct DB connection */
	char	   *archdbname;		/* DB name *read* from archive */
	char	   *savedPassword;	/* password for ropt->username, if known */
	char	   *use_role;
	PGconn	   *connection;
	/* If connCancel isn't NULL, SIGINT handler will send a cancel */
	PGcancel   *volatile connCancel;

	int			connectToDB;	/* Flag to indicate if direct DB connection is
								 * required */
	ArchiverOutput outputKind;	/* Flag for what we're currently writing */
	bool		pgCopyIn;		/* Currently in libpq 'COPY IN' mode. */

	int			loFd;
	bool		writingLO;
	int			loCount;		/* # of LOs restored */

	char	   *fSpec;			/* Archive File Spec */
	FILE	   *FH;				/* General purpose file handle */
	void	   *OF;				/* Output file */

	struct _tocEntry *toc;		/* Header of circular list of TOC entries */
	int			tocCount;		/* Number of TOC entries */
	DumpId		maxDumpId;		/* largest DumpId among all TOC entries */

	/* arrays created after the TOC list is complete: */
	struct _tocEntry **tocsByDumpId;	/* TOCs indexed by dumpId */
	DumpId	   *tableDataId;	/* TABLE DATA ids, indexed by table dumpId */

	struct _tocEntry *currToc;	/* Used when dumping data */
	pg_compress_specification compression_spec; /* Requested specification for
												 * compression */
	bool		dosync;			/* data requested to be synced on sight */
	DataDirSyncMethod sync_method;
	ArchiveMode mode;			/* File mode - r or w */
	void	   *formatData;		/* Header data specific to file format */

	/* these vars track state to avoid sending redundant SET commands */
	char	   *currUser;		/* current username, or NULL if unknown */
	char	   *currSchema;		/* current schema, or NULL */
	char	   *currTablespace; /* current tablespace, or NULL */
	char	   *currTableAm;	/* current table access method, or NULL */

	/* in --transaction-size mode, this counts objects emitted in cur xact */
	int			txnCount;

	void	   *lo_buf;
	size_t		lo_buf_used;
	size_t		lo_buf_size;

	int			noTocComments;
	ArchiverStage stage;
	ArchiverStage lastErrorStage;
	struct _tocEntry *currentTE;
	struct _tocEntry *lastErrorTE;
} ;


struct _tocEntry
{
	struct _tocEntry *prev;
	struct _tocEntry *next;
	CatalogId	catalogId;
	DumpId		dumpId;
	teSection	section;
	bool		hadDumper;		/* Archiver was passed a dumper routine (used
								 * in restore) */
	char	   *tag;			/* index tag */
	char	   *namespace;		/* null or empty string if not in a schema */
	char	   *tablespace;		/* null if not in a tablespace; empty string
								 * means use database default */
	char	   *tableam;		/* table access method, only for TABLE tags */
	char		relkind;		/* relation kind, only for TABLE tags */
	char	   *owner;
	char	   *desc;
	char	   *defn;
	char	   *dropStmt;
	char	   *copyStmt;
	DumpId	   *dependencies;	/* dumpIds of objects this one depends on */
	int			nDeps;			/* number of dependencies */

	DataDumperPtr dataDumper;	/* Routine to dump data for object */
	const void *dataDumperArg;	/* Arg for above routine */
	void	   *formatData;		/* TOC Entry data specific to file format */

	/* working state while dumping/restoring */
	pgoff_t		dataLength;		/* item's data size; 0 if none or unknown */
	int			reqs;			/* do we need schema and/or data of object
								 * (REQ_* bit mask) */
	bool		created;		/* set for DATA member if TABLE was created */

	/* working state (needed only for parallel restore) */
	struct _tocEntry *pending_prev; /* list links for pending-items list; */
	struct _tocEntry *pending_next; /* NULL if not in that list */
	int			depCount;		/* number of dependencies not yet restored */
	DumpId	   *revDeps;		/* dumpIds of objects depending on this one */
	int			nRevDeps;		/* number of such dependencies */
	DumpId	   *lockDeps;		/* dumpIds of objects this one needs lock on */
	int			nLockDeps;		/* number of such dependencies */
};




typedef struct _extensionInfo
{
	DumpableObject dobj;
	char	   *namespace;		/* schema containing extension's objects */
	bool		relocatable;
	char	   *extversion;
	char	   *extconfig;		/* info about configuration tables */
	char	   *extcondition;
} ExtensionInfo;








/*
 *	We may want to have some more user-readable data, but in the mean
 *	time this gives us some abstraction and type checking.
 */







typedef struct _tableInfo
{
	/*
	 * These fields are collected for every table in the database.
	 */
	DumpableObject dobj;
	DumpableAcl dacl;
	const char *rolname;
	char		relkind;
	char		relpersistence; /* relation persistence */
	bool		relispopulated; /* relation is populated */
	char		relreplident;	/* replica identifier */
	char	   *reltablespace;	/* relation tablespace */
	char	   *reloptions;		/* options specified by WITH (...) */
	char	   *checkoption;	/* WITH CHECK OPTION, if any */
	char	   *toast_reloptions;	/* WITH options for the TOAST table */
	bool		hasindex;		/* does it have any indexes? */
	bool		hasrules;		/* does it have any rules? */
	bool		hastriggers;	/* does it have any triggers? */
	bool		hascolumnACLs;	/* do any columns have non-default ACLs? */
	bool		rowsec;			/* is row security enabled? */
	bool		forcerowsec;	/* is row security forced? */
	bool		hasoids;		/* does it have OIDs? */
	uint32_t		frozenxid;		/* table's relfrozenxid */
	uint32_t		minmxid;		/* table's relminmxid */
	Oid			toast_oid;		/* toast table's OID, or 0 if none */
	uint32_t		toast_frozenxid;	/* toast table's relfrozenxid, if any */
	uint32_t		toast_minmxid;	/* toast table's relminmxid */
	int			ncheck;			/* # of CHECK expressions */
	Oid			reltype;		/* OID of table's composite type, if any */
	Oid			reloftype;		/* underlying type for typed table */
	Oid			foreign_server; /* foreign server oid, if applicable */
	/* these two are set only if table is a sequence owned by a column: */
	Oid			owning_tab;		/* OID of table owning sequence */
	int			owning_col;		/* attr # of column owning sequence */
	bool		is_identity_sequence;
	int			relpages;		/* table's size in pages (from pg_class) */
	int			toastpages;		/* toast table's size in pages, if any */

	bool		interesting;	/* true if need to collect more data */
	bool		dummy_view;		/* view's real definition must be postponed */
	bool		postponed_def;	/* matview must be postponed into post-data */
	bool		ispartition;	/* is table a partition? */
	bool		unsafe_partitions;	/* is it an unsafe partitioned table? */

	int			numParents;		/* number of (immediate) parent tables */
	struct _tableInfo **parents;	/* TableInfos of immediate parents */

	/*
	 * These fields are computed only if we decide the table is interesting
	 * (it's either a table to dump, or a direct parent of a dumpable table).
	 */
	int			numatts;		/* number of attributes */
	char	  **attnames;		/* the attribute names */
	char	  **atttypnames;	/* attribute type names */
	int		   *attstattarget;	/* attribute statistics targets */
	char	   *attstorage;		/* attribute storage scheme */
	char	   *typstorage;		/* type storage scheme */
	bool	   *attisdropped;	/* true if attr is dropped; don't dump it */
	char	   *attidentity;
	char	   *attgenerated;
	int		   *attlen;			/* attribute length, used by binary_upgrade */
	char	   *attalign;		/* attribute align, used by binary_upgrade */
	bool	   *attislocal;		/* true if attr has local definition */
	char	  **attoptions;		/* per-attribute options */
	Oid		   *attcollation;	/* per-attribute collation selection */
	char	   *attcompression; /* per-attribute compression method */
	char	  **attfdwoptions;	/* per-attribute fdw options */
	char	  **attmissingval;	/* per attribute missing value */
	bool	   *notnull;		/* not-null constraints on attributes */
	bool	   *inhNotNull;		/* true if NOT NULL is inherited */
	struct _attrDefInfo **attrdefs; /* DEFAULT expressions */
	struct _constraintInfo *checkexprs; /* CHECK constraints */
	bool		needs_override; /* has GENERATED ALWAYS AS IDENTITY */
	char	   *amname;			/* relation access method */

	/*
	 * Stuff computed only for dumpable tables.
	 */
	int			numIndexes;		/* number of indexes */
	struct _indxInfo *indexes;	/* indexes */
	struct _tableDataInfo *dataObj; /* TableDataInfo, if dumping its data */
	int			numTriggers;	/* number of triggers for table */
	struct _triggerInfo *triggers;	/* array of TriggerInfo structs */
} TableInfo;


typedef struct _tableAttachInfo
{
	DumpableObject dobj;
	TableInfo  *parentTbl;		/* link to partitioned table */
	TableInfo  *partitionTbl;	/* link to partition */
} TableAttachInfo;


typedef struct _tableDataInfo
{
	DumpableObject dobj;
	TableInfo  *tdtable;		/* link to table to dump */
	char	   *filtercond;		/* WHERE condition to limit rows dumped */
} TableDataInfo;

typedef struct _indxInfo
{
	DumpableObject dobj;
	TableInfo  *indextable;		/* link to table the index is for */
	char	   *indexdef;
	char	   *tablespace;		/* tablespace in which index is stored */
	char	   *indreloptions;	/* options specified by WITH (...) */
	char	   *indstatcols;	/* column numbers with statistics */
	char	   *indstatvals;	/* statistic values for columns */
	int			indnkeyattrs;	/* number of index key attributes */
	int			indnattrs;		/* total number of index attributes */
	Oid		   *indkeys;		/* In spite of the name 'indkeys' this field
								 * contains both key and nonkey attributes */
	bool		indisclustered;
	bool		indisreplident;
	bool		indnullsnotdistinct;
	Oid			parentidx;		/* if a partition, parent index OID */
	SimplePtrList partattaches; /* if partitioned, partition attach objects */

	/* if there is an associated constraint object, its dumpId: */
	DumpId		indexconstraint;
} IndxInfo;

typedef struct _attrDefInfo
{
	DumpableObject dobj;		/* note: dobj.name is name of table */
	TableInfo  *adtable;		/* link to table of attribute */
	int			adnum;
	char	   *adef_expr;		/* decompiled DEFAULT expression */
	bool		separate;		/* true if must dump as separate item */
} AttrDefInfo;

typedef struct _indexAttachInfo
{
	DumpableObject dobj;
	IndxInfo   *parentIdx;		/* link to index on partitioned table */
	IndxInfo   *partitionIdx;	/* link to index on partition */
} IndexAttachInfo;

typedef struct _statsExtInfo
{
	DumpableObject dobj;
	const char *rolname;		/* owner */
	TableInfo  *stattable;		/* link to table the stats are for */
	int			stattarget;		/* statistics target */
} StatsExtInfo;

typedef struct _ruleInfo
{
	DumpableObject dobj;
	TableInfo  *ruletable;		/* link to table the rule is for */
	char		ev_type;
	bool		is_instead;
	char		ev_enabled;
	bool		separate;		/* true if must dump as separate item */
	/* separate is always true for non-ON SELECT rules */
} RuleInfo;

typedef struct _triggerInfo
{
	DumpableObject dobj;
	TableInfo  *tgtable;		/* link to table the trigger is for */
	char		tgenabled;
	bool		tgispartition;
	char	   *tgdef;
} TriggerInfo;

typedef struct _evttriggerInfo
{
	DumpableObject dobj;
	char	   *evtname;
	char	   *evtevent;
	const char *evtowner;
	char	   *evttags;
	char	   *evtfname;
	char		evtenabled;
} EventTriggerInfo;

/*
 * struct ConstraintInfo is used for all constraint types.  However we
 * use a different objType for foreign key constraints, to make it easier
 * to sort them the way we want.
 *
 * Note: condeferrable and condeferred are currently only valid for
 * unique/primary-key constraints.  Otherwise that info is in condef.
 */
typedef struct _constraintInfo
{
	DumpableObject dobj;
	TableInfo  *contable;		/* NULL if domain constraint */
	TypeInfo   *condomain;		/* NULL if table constraint */
	char		contype;
	char	   *condef;			/* definition, if CHECK or FOREIGN KEY */
	Oid			confrelid;		/* referenced table, if FOREIGN KEY */
	DumpId		conindex;		/* identifies associated index if any */
	bool		condeferrable;	/* true if constraint is DEFERRABLE */
	bool		condeferred;	/* true if constraint is INITIALLY DEFERRED */
	bool		conislocal;		/* true if constraint has local definition */
	bool		separate;		/* true if must dump as separate item */
} ConstraintInfo;

typedef struct _procLangInfo
{
	DumpableObject dobj;
	DumpableAcl dacl;
	bool		lanpltrusted;
	Oid			lanplcallfoid;
	Oid			laninline;
	Oid			lanvalidator;
	const char *lanowner;
} ProcLangInfo;

typedef struct _castInfo
{
	DumpableObject dobj;
	Oid			castsource;
	Oid			casttarget;
	Oid			castfunc;
	char		castcontext;
	char		castmethod;
} CastInfo;

typedef struct _transformInfo
{
	DumpableObject dobj;
	Oid			trftype;
	Oid			trflang;
	Oid			trffromsql;
	Oid			trftosql;
} TransformInfo;

/* InhInfo isn't a DumpableObject, just temporary state */
typedef struct _inhInfo
{
	Oid			inhrelid;		/* OID of a child table */
	Oid			inhparent;		/* OID of its parent */
} InhInfo;

typedef struct _prsInfo
{
	DumpableObject dobj;
	Oid			prsstart;
	Oid			prstoken;
	Oid			prsend;
	Oid			prsheadline;
	Oid			prslextype;
} TSParserInfo;


typedef struct
{
	const char *descr;			/* comment for an object */
	Oid			classoid;		/* object class (catalog OID) */
	Oid			objoid;			/* object OID */
	int			objsubid;		/* subobject (table column #) */
} CommentItem;

typedef struct
{
	const char *provider;		/* label provider of this security label */
	const char *label;			/* security label for an object */
	Oid			classoid;		/* object class (catalog OID) */
	Oid			objoid;			/* object OID */
	int			objsubid;		/* subobject (table column #) */
} SecLabelItem;

typedef struct _dictInfo
{
	DumpableObject dobj;
	const char *rolname;
	Oid			dicttemplate;
	char	   *dictinitoption;
} TSDictInfo;

typedef struct _tmplInfo
{
	DumpableObject dobj;
	Oid			tmplinit;
	Oid			tmpllexize;
} TSTemplateInfo;

typedef struct _cfgInfo
{
	DumpableObject dobj;
	const char *rolname;
	Oid			cfgparser;
} TSConfigInfo;

typedef struct _fdwInfo
{
	DumpableObject dobj;
	DumpableAcl dacl;
	const char *rolname;
	char	   *fdwhandler;
	char	   *fdwvalidator;
	char	   *fdwoptions;
} FdwInfo;

typedef struct _foreignServerInfo
{
	DumpableObject dobj;
	DumpableAcl dacl;
	const char *rolname;
	Oid			srvfdw;
	char	   *srvtype;
	char	   *srvversion;
	char	   *srvoptions;
} ForeignServerInfo;

typedef struct _defaultACLInfo
{
	DumpableObject dobj;
	DumpableAcl dacl;
	const char *defaclrole;
	char		defaclobjtype;
} DefaultACLInfo;

typedef struct _PublicationInfo
{
	DumpableObject dobj;
	const char *rolname;
	bool		puballtables;
	bool		pubinsert;
	bool		pubupdate;
	bool		pubdelete;
	bool		pubtruncate;
	bool		pubviaroot;
} PublicationInfo;

typedef struct _catalogIdMapEntry
{
	CatalogId	catId;			/* the indexed CatalogId */
	uint32		status;			/* hash status */
	uint32		hashval;		/* hash code for the CatalogId */
	DumpableObject *dobj;		/* the associated DumpableObject, if any */
	ExtensionInfo *ext;			/* owning extension, if any */
} CatalogIdMapEntry;

typedef struct _policyInfo
{
	DumpableObject dobj;
	TableInfo  *poltable;
	char	   *polname;		/* null indicates RLS is enabled on rel */
	char		polcmd;
	bool		polpermissive;
	char	   *polroles;
	char	   *polqual;
	char	   *polwithcheck;
} PolicyInfo;

typedef struct _PublicationRelInfo
{
	DumpableObject dobj;
	PublicationInfo *publication;
	TableInfo  *pubtable;
	char	   *pubrelqual;
	char	   *pubrattrs;
} PublicationRelInfo;

typedef struct _PublicationSchemaInfo
{
	DumpableObject dobj;
	PublicationInfo *publication;
	NamespaceInfo *pubschema;
} PublicationSchemaInfo;

typedef struct _SubscriptionInfo
{
	DumpableObject dobj;
	const char *rolname;
	char	   *subenabled;
	char	   *subbinary;
	char	   *substream;
	char	   *subtwophasestate;
	char	   *subdisableonerr;
	char	   *subpasswordrequired;
	char	   *subrunasowner;
	char	   *subconninfo;
	char	   *subslotname;
	char	   *subsynccommit;
	char	   *subpublications;
	char	   *suborigin;
	char	   *suboriginremotelsn;
	char	   *subfailover;
} SubscriptionInfo;

typedef struct _SubRelInfo
{
	DumpableObject dobj;
	SubscriptionInfo *subinfo;
	TableInfo  *tblinfo;
	char		srsubstate;
	char	   *srsublsn;
} SubRelInfo;


Archive *CreateArchive(const char *FileSpec, const ArchiveFormat fmt,
							  const pg_compress_specification compression_spec,
							  bool dosync, ArchiveMode mode,
							  SetupWorkerPtrType setupDumpWorker,
							  DataDirSyncMethod sync_method);

void
InitDumpOptions(DumpOptions *opts);

void
SetArchiveOptions(Archive *AH, DumpOptions *dopt, RestoreOptions *ropt);


int	ExecuteSqlCommandBuf(Archive *AHX, const char *buf, size_t bufLen);

void ExecuteSqlStatement(Archive *AHX, const char *query);
PGresult *ExecuteSqlQuery(Archive *AHX, const char *query,
								 ExecStatusType status);
PGresult *ExecuteSqlQueryForSingleRow(Archive *fout, const char *query);

extern void warn_or_exit_horribly(ArchiveHandle *AH, const char *fmt,...) pg_attribute_printf(2, 3);

void
ConnectDatabase(Archive *AHX,
				const ConnParams *cparams,
				bool isReconnect);

				
PGconn *
GetConnection(Archive *AHX);



NamespaceInfo *getNamespaces(Archive *fout, int *numNamespaces);
ExtensionInfo *getExtensions(Archive *fout, int *numExtensions);
TypeInfo *getTypes(Archive *fout, int *numTypes);
FuncInfo *getFuncs(Archive *fout, int *numFuncs);
AggInfo *getAggregates(Archive *fout, int *numAggs);
OprInfo *getOperators(Archive *fout, int *numOprs);
AccessMethodInfo *getAccessMethods(Archive *fout, int *numAccessMethods);
OpclassInfo *getOpclasses(Archive *fout, int *numOpclasses);
OpfamilyInfo *getOpfamilies(Archive *fout, int *numOpfamilies);
CollInfo *getCollations(Archive *fout, int *numCollations);
ConvInfo *getConversions(Archive *fout, int *numConversions);
TableInfo *getTables(Archive *fout, int *numTables);
void getOwnedSeqs(Archive *fout, TableInfo tblinfo[], int numTables);
InhInfo *getInherits(Archive *fout, int *numInherits);
void getPartitioningInfo(Archive *fout);
void getIndexes(Archive *fout, TableInfo tblinfo[], int numTables);
void getExtendedStatistics(Archive *fout);
void getConstraints(Archive *fout, TableInfo tblinfo[], int numTables);
RuleInfo *getRules(Archive *fout, int *numRules);
void getTriggers(Archive *fout, TableInfo tblinfo[], int numTables);
ProcLangInfo *getProcLangs(Archive *fout, int *numProcLangs);
CastInfo *getCasts(Archive *fout, int *numCasts);
TransformInfo *getTransforms(Archive *fout, int *numTransforms);
void getTableAttrs(Archive *fout, TableInfo *tblinfo, int numTables);
bool shouldPrintColumn(const DumpOptions *dopt, const TableInfo *tbinfo, int colno);
TSParserInfo *getTSParsers(Archive *fout, int *numTSParsers);
TSDictInfo *getTSDictionaries(Archive *fout, int *numTSDicts);
TSTemplateInfo *getTSTemplates(Archive *fout, int *numTSTemplates);
TSConfigInfo *getTSConfigurations(Archive *fout, int *numTSConfigs);
FdwInfo *getForeignDataWrappers(Archive *fout,
									int *numForeignDataWrappers);
ForeignServerInfo *getForeignServers(Archive *fout,
										int *numForeignServers);
DefaultACLInfo *getDefaultACLs(Archive *fout, int *numDefaultACLs);
void getExtensionMembership(Archive *fout, ExtensionInfo extinfo[],
								int numExtensions);
void processExtensionTables(Archive *fout, ExtensionInfo extinfo[],
								int numExtensions);
EventTriggerInfo *getEventTriggers(Archive *fout, int *numEventTriggers);
void getPolicies(Archive *fout, TableInfo tblinfo[], int numTables);
PublicationInfo *getPublications(Archive *fout,
									int *numPublications);
void getPublicationNamespaces(Archive *fout);
void getPublicationTables(Archive *fout, TableInfo tblinfo[],
								int numTables);
void getSubscriptions(Archive *fout);
void getSubscriptionTables(Archive *fout);
static void
getDomainConstraints(Archive *fout, TypeInfo *tyinfo);
static void
addConstrChildIdxDeps(DumpableObject *dobj, const IndxInfo *refidx);

const char *
getRoleName(const char *roleoid_str);

TableInfo *
findTableByOid(Oid oid);

DumpableObject *
findObjectByCatalogId(CatalogId catalogId);

void flagInhTables(Archive *fout, TableInfo *tblinfo, int numTables,
						  InhInfo *inhinfo, int numInherits);
void flagInhIndexes(Archive *fout, TableInfo *tblinfo, int numTables);
void flagInhAttrs(Archive *fout, TableInfo *tblinfo, int numTables);
int	strInArray(const char *pattern, char **arr, int arr_size);

void
AssignDumpId(DumpableObject *dobj);


PublicationInfo *
getPublications(Archive *fout, int *numPublications);

void
getSubscriptions(Archive *fout);

void
getSubscriptionTables(Archive *fout);


void
collectRoleNames(Archive *fout);

void
quoteAclUserName(PQExpBuffer output, const char *input);

NamespaceInfo *
findNamespaceByOid(Oid oid);

ExtensionInfo *
findExtensionByOid(Oid oid);

void
parseOidArray(const char *str, Oid *array, int arraysize);

void
addObjectDependency(DumpableObject *dobj, DumpId refId);


TypeInfo *
findTypeByOid(Oid oid);

void
makeTableDataInfo(DumpOptions *dopt, TableInfo *tbinfo);


IndxInfo *
findIndexByOid(Oid oid);

int
strInArray(const char *pattern, char **arr, int arr_size);

PublicationInfo *
findPublicationByOid(Oid oid);

SubscriptionInfo *
findSubscriptionByOid(Oid oid);

bool
is_superuser(Archive *fout);

bool
checkExtensionMembership(DumpableObject *dobj, Archive *fout);

ExtensionInfo *
findOwningExtension(CatalogId catalogId);


DumpableObject *
createBoundaryObjects(void);

void
getDumpableObjects(DumpableObject ***objs, int *numObjs);

static int
DOTypeNameCompare(const void *p1, const void *p2);

void
sortDumpableObjects(DumpableObject **objs, int numObjs,
					DumpId preBoundaryId, DumpId postBoundaryId);

void
sortDumpableObjectsByTypeName(DumpableObject **objs, int numObjs);

DumpableObject *
findObjectByDumpId(DumpId dumpId);




typedef struct _archiveOpts
{
	const char *tag;
	const char *namespace;
	const char *tablespace;
	const char *tableam;
	char		relkind;
	const char *owner;
	const char *description;
	teSection	section;
	const char *createStmt;
	const char *dropStmt;
	const char *copyStmt;
	const DumpId *deps;
	int			nDeps;
	DataDumperPtr dumpFn;
	const void *dumpArg;
} ArchiveOpts;
#define ARCHIVE_OPTS(...) &(ArchiveOpts){__VA_ARGS__}



typedef struct _loInfo
{
	DumpableObject dobj;
	DumpableAcl dacl;
	const char *rolname;
	int			numlos;
	Oid			looids[FLEXIBLE_ARRAY_MEMBER];
} LoInfo;



void
dumpDumpableObject(Archive *fout, DumpableObject *dobj);



CollInfo *
findCollationByOid(Oid oid);

FuncInfo *
findFuncByOid(Oid oid);

bool
variable_is_guc_list_quote(const char *name);

int
EndLO(Archive *AHX, Oid oid);

int
StartLO(Archive *AHX, Oid oid);


bool
SplitGUCList(char *rawstring, char separator,
			 char ***namelist);

OprInfo *
findOprByOid(Oid oid);

bool
buildACLCommands(const char *name, const char *subname, const char *nspname,
				 const char *type, const char *acls, const char *baseacls,
				 const char *owner, const char *prefix, int remoteVersion,
				 PQExpBuffer sql);


bool
parseAclItem(const char *item, const char *type,
			 const char *name, const char *subname, int remoteVersion,
			 PQExpBuffer grantee, PQExpBuffer grantor,
			 PQExpBuffer privs, PQExpBuffer privswgo);


void
WriteData(Archive *AHX, const void *data, size_t dLen);


void
AddAcl(PQExpBuffer aclbuf, const char *keyword, const char *subname);

RestoreOptions *
NewRestoreOptions(void);

void
ProcessArchiveRestoreOptions(Archive *AHX);

void
InitCompressFileHandleNone(CompressFileHandle *CFH,
						   const pg_compress_specification compression_spec);

void
ExecuteSqlCommand(ArchiveHandle *AH, const char *qry, const char *desc);


typedef struct {
    char** tableNames;
    int tableCount;
    char **schemas;
    int schemaCount;
	Oid* viewOids;
	int viewCount;
	bool been_analyzed;
} QueryDependencies;

QueryDependencies* analyze_query_dependencies(const char *query);
QueryDependencies* extract_tables_from_query_text(PGconn* conn, const char *query, QueryDependencies* deps);

extern QueryDependencies* deps;

void
add_table(QueryDependencies *deps, const char *name);

void add_schema_to_deps(QueryDependencies *deps, const char *schema);


extern SimpleStringList table_include_patterns;
extern SimpleOidList table_include_oids;
extern int	strict_names;
extern bool no_checks;
extern bool no_exts;
QueryDependencies* InitQueryDependencies();
void export_stats(PGconn* conn, const char* export_filename);
void export_constraints(PGconn* conn, const char* export_filename);

void get_explain(PGconn* conn, const char *query, char* filename, bool analyze);

typedef struct {
	char* name;
	char* setting;
	char* unit;
} PlannerSetting;

char* get_planner_settings(Archive* AH);

void find_views_for_tables(PGconn* conn, QueryDependencies *deps, char* query_text);

bool is_in_view_oids(Oid oid);

void mark_views_for_dump(TableInfo *tblinfo, int numTables, QueryDependencies *deps);

TableInfo *
getSchemaData(Archive *fout, int *numTablesPtr, QueryDependencies* deps);

void
RestoreArchive(Archive *AHX);

void
CloseArchive(Archive *AHX);

TocEntry *
ArchiveEntry(Archive *AHX, CatalogId catalogId, DumpId dumpId,
			 ArchiveOpts *opts);

DumpId
getMaxDumpId(void);

 void
selectDumpableObject(DumpableObject *dobj, Archive *fout);

void
selectDumpableStatisticsObject(StatsExtInfo *sobj, Archive *fout);

void
selectDumpableNamespace(NamespaceInfo *nsinfo, Archive *fout);

 void
selectDumpableExtension(ExtensionInfo *extinfo, DumpOptions *dopt);

void
selectDumpableType(TypeInfo *tyinfo, Archive *fout);

 void
selectDumpableAccessMethod(AccessMethodInfo *method, Archive *fout);

 void
selectDumpableTable(TableInfo *tbinfo, Archive *fout);

 void
selectDumpableProcLang(ProcLangInfo *plang, Archive *fout);

 void
selectDumpableCast(CastInfo *cast, Archive *fout);

void
selectDumpableDefaultACL(DefaultACLInfo *dinfo, DumpOptions *dopt);

void
recordExtensionMembership(CatalogId catId, ExtensionInfo *ext);

 void
selectDumpablePublicationObject(DumpableObject *dobj, Archive *fout);

DumpId
createDumpId(void);


extern int ahprintf(ArchiveHandle *AH, const char *fmt,...) pg_attribute_printf(2, 3);
extern void ahwrite(const void *ptr, size_t size, size_t nmemb, ArchiveHandle *AH);
extern void mark_schemas_for_dump(TableInfo *tblinfo, int numTables);
extern bool buildDefaultACLCommands(const char *type, const char *nspname,
            const char *acls, const char *acldefault,
            const char *owner,
            int remoteVersion,
            PQExpBuffer sql);