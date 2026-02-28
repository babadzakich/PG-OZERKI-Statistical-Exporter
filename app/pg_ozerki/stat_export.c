#include "pg_ozerki.h"

#define COL_NUM 16

#define BUF_SIZE 1024



void export_stats(PGconn* conn, const char* export_filename) {
    FILE* fp = fopen(export_filename, "w");

    if (!fp) {
        pg_log_error("Could not open file for stats export");
        return;
    }
    PQExpBuffer statQuery;
    statQuery = createPQExpBuffer();
    
    appendPQExpBuffer(statQuery, 
       "WITH table_counts AS ( "
    "    SELECT  "
    "        n.nspname AS schemaname, "
    "        c.relname AS table_name, "
    "        c.reltuples::bigint AS row_count "
    "    FROM pg_class c "
    "    JOIN pg_namespace n ON n.oid = c.relnamespace "
    "    WHERE c.relkind = 'r' "
    "      AND n.nspname NOT IN ('pg_catalog', 'information_schema') "
    "), "
    "column_flags AS ( "
    "    SELECT "
    "        a.attrelid, "
    "        a.attname, "
    "        a.attnum, "
    "        BOOL_OR(c.contype = 'p') AS is_pk, "
    "        BOOL_OR(c.contype = 'u') AS is_unique_con, "
    "        BOOL_OR(EXISTS ( "
    "            SELECT 1 FROM pg_index i  "
    "            WHERE i.indrelid = a.attrelid AND a.attnum = ANY(i.indkey)  "
    "            AND i.indisunique AND NOT i.indisprimary "
    "        )) AS is_unique_idx "
    "    FROM pg_attribute a "
    "    LEFT JOIN pg_constraint c ON c.conrelid = a.attrelid AND a.attnum = ANY(c.conkey) "
    "    WHERE a.attnum > 0 AND NOT a.attisdropped "
    "    GROUP BY a.attrelid, a.attname, a.attnum "
    "), "
    "fk_constraints AS ( "
    "    SELECT "
    "        con.conrelid, "
    "        a_src.attname AS src_column, "
    "        conf_ns.nspname || '.' || conf_tbl.relname || '.' || a_ref.attname AS target_path, "
    "        CASE  "
    "            WHEN (src_f.is_pk OR src_f.is_unique_con OR src_f.is_unique_idx)  "
    "            THEN 'ONE_TO_ONE' ELSE 'ONE_TO_MANY'  "
    "        END AS rel_type "
    "    FROM pg_constraint con "
    "    CROSS JOIN LATERAL unnest(con.conkey, con.confkey) AS pairs(src_num, ref_num) "
    "    JOIN pg_attribute a_src ON a_src.attrelid = con.conrelid AND a_src.attnum = pairs.src_num "
    "    JOIN pg_attribute a_ref ON a_ref.attrelid = con.confrelid AND a_ref.attnum = pairs.ref_num "
    "    JOIN pg_class conf_tbl ON conf_tbl.oid = con.confrelid "
    "    JOIN pg_namespace conf_ns ON conf_ns.oid = conf_tbl.relnamespace "
    "    JOIN column_flags src_f ON src_f.attrelid = con.conrelid AND src_f.attnum = pairs.src_num "
    "    WHERE con.contype = 'f' "
    "), "
    "aggregated_references AS ( "
    "    SELECT  "
    "        conrelid, "
    "        src_column, "
    "        string_agg(target_path, ', ') AS outcoming_references, "
    "        string_agg(rel_type, ', ') AS relation_types "
    "    FROM fk_constraints "
    "    GROUP BY conrelid, src_column "
    "), "
    "composite_unique_info AS ( "
    "    SELECT  "
    "        a.attrelid, "
    "        a.attname, "
    "        ( "
    "            SELECT string_agg(DISTINCT n.nspname || '.' || c.relname || '.' || a_other.attname, ', ') "
    "            FROM ( "
    "                SELECT unnest(conkey) as col_num, conrelid as rel_id "
    "                FROM pg_constraint  "
    "                WHERE contype IN ('u', 'p') AND array_length(conkey, 1) > 1 "
    "                UNION ALL "
    "                SELECT unnest(indkey) as col_num, indrelid as rel_id "
    "                FROM pg_index  "
    "                WHERE indisunique = true AND array_length(indkey, 1) > 1 "
    "            ) sub "
    "    JOIN pg_class c ON c.oid = sub.rel_id "
"            JOIN pg_namespace n ON n.oid = c.relnamespace "
    "            JOIN pg_attribute a_other ON a_other.attrelid = sub.rel_id AND a_other.attnum = sub.col_num "
    "            WHERE sub.rel_id = a.attrelid  "
    "              AND a_other.attname <> a.attname "
    "              AND EXISTS ( "
    "                  SELECT 1 FROM ( "
    "                      SELECT conkey as keys, conrelid as rid FROM pg_constraint WHERE contype IN ('u', 'p') "
    "                      UNION ALL "
    "                      SELECT indkey as keys, indrelid as rid FROM pg_index WHERE indisunique = true "
    "                  ) check_sub  "
    "                  WHERE rid = a.attrelid AND a.attnum = ANY(keys) AND a_other.attnum = ANY(keys) "
    "              ) "
    "        ) as composite_unique_peers "
    "    FROM pg_attribute a "
    "    WHERE a.attnum > 0 AND NOT a.attisdropped "
    "), "
    "composite_fk_info AS ( "
    "    SELECT  "
    "        a.attrelid, "
    "        a.attname, "
    "        string_agg(DISTINCT n.nspname || '.' || c.relname || '.' || peer.attname, ', ') as peers "
    "    FROM pg_attribute a "
     " JOIN pg_class c ON c.oid = a.attrelid "
   " JOIN pg_namespace n ON n.oid = c.relnamespace "
    "    JOIN pg_constraint con ON con.conrelid = a.attrelid AND a.attnum = ANY(con.conkey) "
    "    JOIN pg_attribute peer ON peer.attrelid = a.attrelid AND peer.attnum = ANY(con.conkey) AND peer.attnum <> a.attnum "
    "    WHERE con.contype = 'f'  "
    "      AND array_length(con.conkey, 1) > 1 "
    "      AND a.attnum > 0  "
    "      AND NOT a.attisdropped "
    "    GROUP BY a.attrelid, a.attname "
    "), "
    "incoming_references_info AS ( "
    "    SELECT "
    "        con.confrelid AS target_table_oid, "
    "        a_target.attname AS target_col_name, "
    "        string_agg(n_src.nspname || '.' || c_src.relname || '.' || a_src.attname, ', ') AS referring_columns "
    "    FROM pg_constraint con "
    "    JOIN pg_class c_src ON con.conrelid = c_src.oid "
    "    JOIN pg_namespace n_src ON c_src.relnamespace = n_src.oid "
    "    CROSS JOIN LATERAL unnest(con.conkey, con.confkey) AS refs(src_col_num, target_col_num) "
    "    JOIN pg_attribute a_src ON a_src.attrelid = con.conrelid AND a_src.attnum = refs.src_col_num "
    "    JOIN pg_attribute a_target ON a_target.attrelid = con.confrelid AND a_target.attnum = refs.target_col_num "
    "    WHERE con.contype = 'f' "
    "    GROUP BY con.confrelid, a_target.attname "
    "), "
    "column_constraints AS ( "
    "    SELECT "
    "        a.attrelid, "
    "        a.attname, "
    "        a.attnum, "
    "        BOOL_OR(c.contype = 'p') AS is_pk, "
    "        BOOL_OR(c.contype = 'u') AS is_unique_constraint, "
    "        BOOL_OR(c.contype = 'c') AS is_check_constraint, "
    "        BOOL_OR(EXISTS ( "
    "            SELECT 1 FROM pg_index i "
    "            WHERE i.indrelid = a.attrelid "
    "            AND a.attnum = ANY(i.indkey) "
    "            AND i.indisunique = true "
    "            AND i.indisprimary = false "
    "        )) AS is_unique_index "
    "    FROM pg_attribute a "
    "    LEFT JOIN pg_constraint c ON c.conrelid = a.attrelid AND a.attnum = ANY(c.conkey) "
    "    WHERE a.attnum > 0 AND NOT a.attisdropped "
    "    GROUP BY a.attrelid, a.attname, a.attnum "
    "), "
    "column_stats AS ( "
    "    SELECT  "
    "        n.nspname AS table_schema, "
    "        c.relname AS table_name, "
    "        a.attname AS column_name, "
    "        pg_catalog.format_type(a.atttypid, a.atttypmod) AS data_type, "
    "        a.attnum AS column_number, "
    "        a.attrelid as table_oid, "
"    (t.typelem != 0 AND t.typlen = -1) AS is_array, "
    "        CASE  "
    "            WHEN a.atttypid IN (1042, 1043, 25) THEN "
    "                CASE  "
    "                    WHEN a.atttypmod = -1 THEN -1 "
    "                    ELSE a.atttypmod - 4 "
    "                END "
    "            ELSE -1 "
    "        END AS max_length, "
    "        (SELECT COUNT(*) FROM pg_constraint  "
    "         WHERE conrelid = c.oid AND contype = 'p' AND a.attnum = ANY(conkey)) AS is_primary_key, "
    "        (SELECT COUNT(*) FROM pg_constraint  "
    "         WHERE conrelid = c.oid AND contype = 'f' AND a.attnum = ANY(conkey)) AS is_foreign_key, "
    "        (SELECT COUNT(*) FROM pg_constraint  "
    "         WHERE conrelid = c.oid AND contype = 'u' AND a.attnum = ANY(conkey)) AS is_unique, "
    "        (SELECT COUNT(*) FROM pg_constraint  "
    "         WHERE conrelid = c.oid AND contype = 'c' AND a.attnum = ANY(conkey)) AS is_check, "
    "        (SELECT COUNT(*) FROM pg_index  "
    "         WHERE indrelid = c.oid AND indisunique = true AND indisprimary = false  "
    "         AND a.attnum = ANY(indkey)) AS is_unique_index "
    "    FROM pg_attribute a "
    "    JOIN pg_class c ON a.attrelid = c.oid "
    "    JOIN pg_namespace n ON c.relnamespace = n.oid "
"    JOIN pg_type t ON a.atttypid = t.oid "
    "    WHERE a.attnum > 0 "
    "      AND NOT a.attisdropped "
    "      AND c.relkind = 'r' "
    "      AND n.nspname NOT IN ('pg_catalog', 'information_schema') "
   
    
    );


    if (deps->tableCount > 0) {
        appendPQExpBuffer( statQuery, "      AND c.relname IN (");
        char* token;
        for (int i = 0; i < deps->tableCount; i++) {
                if (i > 0) appendPQExpBuffer(statQuery, ", ");
                char *name = pstrdup(deps->tableNames[i]);
                token = strtok(name, ".");
                token = strtok(NULL, ".");
                appendPQExpBuffer(statQuery, "'%s'", token);
                free(name);
                
        }
        appendPQExpBuffer(statQuery, ")"); 
    }

    
    
    appendPQExpBuffer(statQuery,
        ") "
       " SELECT "
        "    cs.table_schema,"
        "    cs.table_name, "
        "    cs.column_name, "
        "    cs.data_type, "
        "    tc.row_count, "
        "    CASE "
        "        WHEN tc.row_count = 0 THEN 0 "
        "        ELSE ROUND((s.null_frac * 100)::numeric, 2) "
        "    END AS null_percent, "
        "    TRIM( "
        "        CASE WHEN cs.is_primary_key > 0 THEN 'PK ' ELSE '' END || "
        "        CASE WHEN cs.is_foreign_key > 0 THEN 'FK ' ELSE '' END || "
        "        CASE WHEN cs.is_unique > 0 OR cs.is_unique_index > 0 THEN 'UNIQUE ' ELSE '' END || "
        "        CASE WHEN cs.is_check > 0 THEN 'CHECK' ELSE '' END "
        "    ) AS modifiers, "
        "    cui.composite_unique_peers, "
        "    cfk.peers AS composite_fk_peers, "
        "    inc.referring_columns AS incoming_references, "
        "    cs.max_length, "
        "    out_ref.outcoming_references, "
        "    out_ref.relation_types, "
"        CASE WHEN cs.is_array THEN s.most_common_elems ELSE s.most_common_vals END AS mcv, "
"    CASE WHEN cs.is_array THEN s.most_common_elem_freqs ELSE s.most_common_freqs END AS mcv_frequencies, " 
        "    s.avg_width AS avg_column_width_bytes, "
        "    s.n_distinct AS ndistinct, "
        "    s.histogram_bounds as hbounds "
        "FROM column_stats cs "
        "JOIN table_counts tc ON cs.table_schema = tc.schemaname AND cs.table_name = tc.table_name "
        "LEFT JOIN aggregated_references out_ref ON out_ref.conrelid = cs.table_oid AND out_ref.src_column = cs.column_name "
        "LEFT JOIN pg_stats s ON s.schemaname = cs.table_schema "
        "                     AND s.tablename = cs.table_name "
        "                     AND s.attname = cs.column_name "
        "LEFT JOIN composite_unique_info cui ON cui.attrelid = cs.table_oid "
        "                                   AND cui.attname = cs.column_name "
        "LEFT JOIN composite_fk_info cfk ON cfk.attrelid = cs.table_oid AND cfk.attname = cs.column_name "
        "LEFT JOIN incoming_references_info inc ON inc.target_table_oid = cs.table_oid AND inc.target_col_name = cs.column_name "
        "ORDER BY "
        "    cs.table_schema, "
        "    cs.table_name, "
        "    cs.column_number "

    );


    PQExpBuffer cop_buf = createPQExpBuffer();
    appendPQExpBuffer(cop_buf, "COPY ( ");
    appendPQExpBuffer(cop_buf, statQuery->data);
    appendPQExpBuffer(cop_buf," ) TO STDOUT WITH (FORMAT CSV, HEADER, NULL 'NULL')");
    PGresult* cop_res = PQexec(conn, cop_buf->data);

    ExecStatusType cop_res_status = PQresultStatus(cop_res);

    
    if (cop_res_status != PGRES_COPY_OUT) {
        pg_log_error("Stats query has failed with result: %s", PQresultErrorMessage(cop_res));
        destroyPQExpBuffer(cop_buf);
        destroyPQExpBuffer(statQuery);
        PQclear(cop_res);
        fclose(fp);
        return;
    }
    char* out_buf;
    int len;

    while ((len = PQgetCopyData(conn, &out_buf, 0)) > 0) {
        fwrite(out_buf, 1, len, fp);
        PQfreemem(out_buf);
    }
    fclose(fp);
    PQclear(cop_res);
    destroyPQExpBuffer(cop_buf);
    destroyPQExpBuffer(statQuery);

}

