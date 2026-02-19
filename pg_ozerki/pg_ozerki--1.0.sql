/* contrib/dump_stat/dump_stat--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_ozerki" to load this file. \quit

CREATE FUNCTION anyarray_elemtype(arr anyarray)
RETURNS oid
AS 'MODULE_PATHNAME', 'anyarray_elemtype'
LANGUAGE C STRICT;

CREATE FUNCTION dump_schema()
RETURNS text
AS 'MODULE_PATHNAME', 'dump_schema'
LANGUAGE C STRICT;

CREATE FUNCTION dump_schema_by_query(query text)
RETURNS text
AS 'MODULE_PATHNAME', 'dump_schema_by_query'
LANGUAGE C STRICT;

CREATE TYPE ozerki_statistic AS
(
    table_schema    text,
    table_name      text,
    column_name     text,
    data_type       text,
    row_count       bigint,
    null_percent    numeric,
    modifiers       text,
    max_length      integer,
    relation_type   text,
    referenced_table text,
    referenced_column text,
    mcv             text,
    mcv_frequencies float4[],
    avg_column_width_bytes int4,
    ndistinct       float4,
    hbounds         text
);

CREATE OR REPLACE FUNCTION dump_statistic_by_query(query text) 
RETURNS SETOF ozerki_statistic
AS 'MODULE_PATHNAME', 'dump_statistic_by_query'
LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION export_query_plan(query text, use_analyze boolean) 
RETURNS TEXT
AS 'MODULE_PATHNAME', 'export_query_plan'
LANGUAGE C STRICT;

CREATE FUNCTION dump_statistic() 
RETURNS TABLE (
    table_schema text,
    table_name text,
    column_name text,
    data_type text,
    row_count bigint,
    null_percent numeric,
    modifiers text,
    composite_unique_peers text,
    composite_fk_peers text,
    incoming_references text,
    max_length integer,
    relation_type text,
    outcoming_reference text,
    mcv text,
    mcv_frequencies float4[],
    avg_column_width_bytes int4,
	ndistinct float4,
    hbounds text
)
LANGUAGE sql
AS $$
SET datestyle TO 'ISO';
SET intervalstyle to 'iso_8601';
WITH table_counts AS (
    SELECT 
        n.nspname AS schemaname,
        c.relname AS table_name,
        c.reltuples::bigint AS row_count
    FROM pg_class c
    JOIN pg_namespace n ON n.oid = c.relnamespace
    WHERE c.relkind = 'r'
      AND n.nspname NOT IN ('pg_catalog', 'information_schema')
),
fk_constraints AS (
    SELECT
        con.conrelid,
        con.confrelid,
        con.conname,
        con.conkey,
        con.confkey,
        a.attname AS column_name,
        a.attnum AS column_num,
        con_tbl.relname AS table_name,
        con_tbl_ns.nspname AS table_schema,
        conf_tbl.relname AS referenced_table,
        conf_tbl_ns.nspname AS referenced_schema,
        (SELECT attname FROM pg_attribute 
         WHERE attrelid = con.confrelid AND attnum = con.confkey[1]) AS referenced_column
    FROM pg_constraint con
    JOIN pg_attribute a ON a.attrelid = con.conrelid AND a.attnum = con.conkey[1]
    JOIN pg_class con_tbl ON con_tbl.oid = con.conrelid
    JOIN pg_namespace con_tbl_ns ON con_tbl_ns.oid = con_tbl.relnamespace
    JOIN pg_class conf_tbl ON conf_tbl.oid = con.confrelid
    JOIN pg_namespace conf_tbl_ns ON conf_tbl_ns.oid = conf_tbl.relnamespace
    WHERE con.contype = 'f'
),

composite_unique_info AS (
    SELECT 
        a.attrelid,
        a.attname,
        (
            SELECT string_agg(DISTINCT a_other.attname, ', ')
            FROM (
                
                SELECT unnest(conkey) as col_num, conrelid as rel_id
                FROM pg_constraint 
                WHERE contype IN ('u', 'p') AND array_length(conkey, 1) > 1
                UNION ALL
                
                SELECT unnest(indkey) as col_num, indrelid as rel_id
                FROM pg_index 
                WHERE indisunique = true AND array_length(indkey, 1) > 1
            ) sub
            JOIN pg_attribute a_other ON a_other.attrelid = sub.rel_id AND a_other.attnum = sub.col_num
            WHERE sub.rel_id = a.attrelid 
              AND a_other.attname <> a.attname
              AND EXISTS (
                  
                  SELECT 1 FROM (
                      SELECT conkey as keys, conrelid as rid FROM pg_constraint WHERE contype IN ('u', 'p')
                      UNION ALL
                      SELECT indkey as keys, indrelid as rid FROM pg_index WHERE indisunique = true
                  ) check_sub 
                  WHERE rid = a.attrelid AND a.attnum = ANY(keys) AND a_other.attnum = ANY(keys)
              )
        ) as composite_unique_peers
    FROM pg_attribute a
    WHERE a.attnum > 0 AND NOT a.attisdropped
),
composite_fk_info AS (
    SELECT 
        a.attrelid,
        a.attname,
        string_agg(DISTINCT peer.attname, ', ') as peers
    FROM pg_attribute a
    JOIN pg_constraint con ON con.conrelid = a.attrelid AND a.attnum = ANY(con.conkey)
    JOIN pg_attribute peer ON peer.attrelid = a.attrelid AND peer.attnum = ANY(con.conkey) AND peer.attnum <> a.attnum
    WHERE con.contype = 'f' 
      AND array_length(con.conkey, 1) > 1
      AND a.attnum > 0 
      AND NOT a.attisdropped
    GROUP BY a.attrelid, a.attname
),

incoming_references_info AS (
    SELECT
        con.confrelid AS target_table_oid,
        a_target.attname AS target_col_name,
        string_agg(n_src.nspname || '.' || c_src.relname || '.' || a_src.attname, ', ') AS referring_columns
    FROM pg_constraint con
    JOIN pg_class c_src ON con.conrelid = c_src.oid
    JOIN pg_namespace n_src ON c_src.relnamespace = n_src.oid
    CROSS JOIN LATERAL unnest(con.conkey, con.confkey) AS refs(src_col_num, target_col_num)
    JOIN pg_attribute a_src ON a_src.attrelid = con.conrelid AND a_src.attnum = refs.src_col_num
    JOIN pg_attribute a_target ON a_target.attrelid = con.confrelid AND a_target.attnum = refs.target_col_num
    WHERE con.contype = 'f'
    GROUP BY con.confrelid, a_target.attname
),
column_constraints AS (
    SELECT
        a.attrelid,
        a.attname,
        a.attnum,
        BOOL_OR(c.contype = 'p') AS is_pk,
        BOOL_OR(c.contype = 'u') AS is_unique_constraint,
        BOOL_OR(c.contype = 'c') AS is_check_constraint,
        BOOL_OR(EXISTS (
            SELECT 1 FROM pg_index i
            WHERE i.indrelid = a.attrelid
            AND a.attnum = ANY(i.indkey)
            AND i.indisunique = true
            AND i.indisprimary = false
        )) AS is_unique_index
    FROM pg_attribute a
    LEFT JOIN pg_constraint c ON c.conrelid = a.attrelid AND a.attnum = ANY(c.conkey)
    WHERE a.attnum > 0 AND NOT a.attisdropped
    GROUP BY a.attrelid, a.attname, a.attnum
),
relation_types AS (
    SELECT
        fk.table_schema,
        fk.table_name,
        fk.column_name,
        fk.referenced_schema,
        fk.referenced_table,
        fk.referenced_column,
        CASE
            WHEN cc.is_pk OR cc.is_unique_constraint OR cc.is_unique_index THEN
                CASE WHEN EXISTS (
                    SELECT 1 FROM column_constraints ref_cc
                    JOIN pg_class ref_tbl ON ref_tbl.oid = ref_cc.attrelid
                    JOIN pg_namespace ref_ns ON ref_ns.oid = ref_tbl.relnamespace
                    WHERE ref_ns.nspname = fk.referenced_schema
                    AND ref_tbl.relname = fk.referenced_table
                    AND ref_cc.attname = fk.referenced_column
                    AND (ref_cc.is_pk OR ref_cc.is_unique_constraint)
                ) THEN 'ONE_TO_ONE' ELSE 'ONE_TO_ONE' END
            ELSE 'ONE_TO_MANY'
        END AS relation_type
    FROM fk_constraints fk
    JOIN column_constraints cc ON cc.attrelid = fk.conrelid AND cc.attname = fk.column_name
),
column_stats AS (
    SELECT 
        n.nspname AS table_schema,
        c.relname AS table_name,
        a.attname AS column_name,
        pg_catalog.format_type(a.atttypid, a.atttypmod) AS data_type,
        a.attnum AS column_number,
        a.attrelid as table_oid,
        CASE 
            WHEN a.atttypid IN (1042, 1043, 25) THEN
                CASE 
                    WHEN a.atttypmod = -1 THEN -1
                    ELSE a.atttypmod - 4
                END
            ELSE -1
        END AS max_length,
        (SELECT COUNT(*) FROM pg_constraint 
         WHERE conrelid = c.oid AND contype = 'p' AND a.attnum = ANY(conkey)) AS is_primary_key,
        (SELECT COUNT(*) FROM pg_constraint 
         WHERE conrelid = c.oid AND contype = 'f' AND a.attnum = ANY(conkey)) AS is_foreign_key,
        (SELECT COUNT(*) FROM pg_constraint 
         WHERE conrelid = c.oid AND contype = 'u' AND a.attnum = ANY(conkey)) AS is_unique,
        (SELECT COUNT(*) FROM pg_constraint 
         WHERE conrelid = c.oid AND contype = 'c' AND a.attnum = ANY(conkey)) AS is_check,
        (SELECT COUNT(*) FROM pg_index 
         WHERE indrelid = c.oid AND indisunique = true AND indisprimary = false 
         AND a.attnum = ANY(indkey)) AS is_unique_index
    FROM pg_attribute a
    JOIN pg_class c ON a.attrelid = c.oid
    JOIN pg_namespace n ON c.relnamespace = n.oid
    WHERE a.attnum > 0
      AND NOT a.attisdropped
      AND c.relkind = 'r'
      AND n.nspname NOT IN ('pg_catalog', 'information_schema')
)
SELECT 
    cs.table_schema,
    cs.table_name,
    cs.column_name,
    cs.data_type,
    tc.row_count,
    CASE 
        WHEN tc.row_count = 0 THEN 0
        ELSE ROUND((s.null_frac * 100)::numeric, 2)
    END AS null_percent,
    TRIM(
        CASE WHEN cs.is_primary_key > 0 THEN 'PK ' ELSE '' END ||
        CASE WHEN cs.is_foreign_key > 0 THEN 'FK ' ELSE '' END ||
        CASE WHEN cs.is_unique > 0 OR cs.is_unique_index > 0 THEN 'UNIQUE ' ELSE '' END ||
        CASE WHEN cs.is_check > 0 THEN 'CHECK' ELSE '' END
    ) AS modifiers,
    cui.composite_unique_peers, 
    cfk.peers AS composite_fk_peers,
    inc.referring_columns AS incoming_references,
    cs.max_length,
    rt.relation_type,
    (rt.referenced_schema || '.' || rt.referenced_table || '.' || rt.referenced_column) AS outcoming_reference,
    s.most_common_vals AS mcv,
    s.most_common_freqs AS mcv_frequencies,
    s.avg_width AS avg_column_width_bytes,
    s.n_distinct AS ndistinct,
    s.histogram_bounds as hbounds
FROM column_stats cs
JOIN table_counts tc ON cs.table_schema = tc.schemaname AND cs.table_name = tc.table_name
LEFT JOIN pg_stats s ON s.schemaname = cs.table_schema 
                     AND s.tablename = cs.table_name 
                     AND s.attname = cs.column_name
LEFT JOIN relation_types rt ON rt.table_schema = cs.table_schema 
                           AND rt.table_name = cs.table_name 
                           AND rt.column_name = cs.column_name
LEFT JOIN composite_unique_info cui ON cui.attrelid = cs.table_oid 
                                   AND cui.attname = cs.column_name
LEFT JOIN composite_fk_info cfk ON cfk.attrelid = cs.table_oid AND cfk.attname = cs.column_name
LEFT JOIN incoming_references_info inc ON inc.target_table_oid = cs.table_oid AND inc.target_col_name = cs.column_name
ORDER BY 
    cs.table_schema,
    cs.table_name,
    cs.column_number
$$;



