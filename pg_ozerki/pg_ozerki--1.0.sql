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

CREATE OR REPLACE FUNCTION export_query_plan(query text) 
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
    max_length integer,
    relation_type text,
    referenced_table text,
    referenced_column text,
    mcv text,
    mcv_frequencies float4[],
    avg_column_width_bytes int4,
	ndistinct float4,
    hbounds text
)
LANGUAGE sql
AS $$
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
        -- Получаем имя referenced колонки
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
column_constraints AS (
    SELECT
        a.attrelid,
        a.attname,
        a.attnum,
        BOOL_OR(c.contype = 'p') AS is_pk,
        BOOL_OR(c.contype = 'u') AS is_unique_constraint,
        -- Проверяем уникальные индексы
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
            -- Если FK колонка является частью PK или уникального ограничения или индекса
            WHEN cc.is_pk OR cc.is_unique_constraint OR cc.is_unique_index THEN
                -- И целевая колонка тоже является частью PK или уникального ограничения
                CASE WHEN EXISTS (
                    SELECT 1 FROM column_constraints ref_cc
                    JOIN pg_class ref_tbl ON ref_tbl.oid = ref_cc.attrelid
                    JOIN pg_namespace ref_ns ON ref_ns.oid = ref_tbl.relnamespace
                    WHERE ref_ns.nspname = fk.referenced_schema
                    AND ref_tbl.relname = fk.referenced_table
                    AND ref_cc.attname = fk.referenced_column
                    AND (ref_cc.is_pk OR ref_cc.is_unique_constraint)
                ) THEN 'ONE_TO_ONE' ELSE 'ONE_TO_ONE' END
            -- Если FK колонка не уникальна, это OTM
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
        ELSE ROUND(
            (s.null_frac * 100)::numeric, 
            2
        )
    END AS null_percent,
    TRIM(
        CASE WHEN cs.is_primary_key > 0 THEN 'PK ' ELSE '' END ||
        CASE WHEN cs.is_foreign_key > 0 THEN 'FK ' ELSE '' END ||
        CASE WHEN cs.is_unique > 0 OR cs.is_unique_index > 0 THEN 'UNIQUE' ELSE '' END
    ) AS modifiers,
    cs.max_length,
    rt.relation_type,
    rt.referenced_table,
    rt.referenced_column,
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
ORDER BY 
    cs.table_schema,
    cs.table_name,
    cs.column_number
$$;



