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

CREATE OR REPLACE FUNCTION to_schema_qualified_operator(opid oid) RETURNS TEXT AS $$
	DECLARE
		result	text;
		r		record;
		ltype	text;
		rtype	text;

	BEGIN
		if opid = 0 then
			return '0';
		end if;
	
		select nspname, oprname, oprleft, oprright
		from pg_catalog.pg_operator inner join pg_catalog.pg_namespace
				on oprnamespace = pg_namespace.oid
		where pg_operator.oid = opid
		into r;
		
		if r is null then
			raise exception '% is not a valid operator id', opid;
		end if;

		if r.oprleft = 0 then
			ltype := 'NONE';
		else
			ltype := to_schema_qualified_type(r.oprleft);
		end if;

		if r.oprright = 0 then
			rtype := 'NONE';
		else
			rtype := to_schema_qualified_type(r.oprright);
		end if;

		return format('%s.%s(%s, %s)',
					  quote_ident(r.nspname), r.oprname,
					  ltype, rtype);
	END;
$$ LANGUAGE plpgsql;


CREATE OR REPLACE FUNCTION to_schema_qualified_type(typid oid) RETURNS TEXT AS $$
	DECLARE
		result text;

	BEGIN
		select quote_ident(nspname) || '.' || quote_ident(typname)
		from pg_catalog.pg_type inner join pg_catalog.pg_namespace
				on typnamespace = pg_namespace.oid
		where pg_type.oid = typid
		into result;
		
		if result is null then
			raise exception '% is not a valid type id', typid;
		end if;

		return result;
	END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION to_schema_qualified_relation(reloid oid) RETURNS TEXT AS $$
	DECLARE
		result text;
		
	BEGIN
		select quote_ident(nspname) || '.' || quote_ident(relname)
		from pg_catalog.pg_class inner join pg_catalog.pg_namespace
				on relnamespace = pg_namespace.oid
		where pg_class.oid = reloid
		into result;
		
		if result is null then
			raise exception '% is not a valid relation id', reloid;
		end if;

		return result;
	END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION to_attname(relation text, colnum int2) RETURNS TEXT AS $$
	DECLARE
		result text;
		
    BEGIN
		select attname
		from pg_catalog.pg_attribute
		where attrelid = relation::regclass and attnum = colnum
		into result;
		
		if result is null then
			raise exception 'attribute #% of relation % not found',
							colnum, quote_literal(relation);
		end if;

		return result;
	END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION to_attnum(relation text, col text) RETURNS INT2 AS $$
	DECLARE
		result int2;
		
    BEGIN
		select attnum
		from pg_catalog.pg_attribute
		where attrelid = relation::regclass and attname = col
		into result;
		
		if result is null then
			raise exception 'attribute % of relation % not found',
							quote_literal(col), quote_literal(relation);
		end if;

		return result;
	END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION to_atttype(relation text, col text) RETURNS TEXT AS $$
	DECLARE
		result text;
		
    BEGIN
		select to_schema_qualified_type(atttypid)
		from pg_catalog.pg_attribute
		where attrelid = relation::regclass and attname = col
		into result;
		
		if result is null then
			raise exception 'attribute % of relation % not found',
							quote_literal(col), quote_literal(relation);
		end if;

		return result;
	END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION to_atttype(relation text, colnum int2) RETURNS TEXT AS $$
	DECLARE
		result text;
		
    BEGIN
		select to_schema_qualified_type(atttypid)
		from pg_catalog.pg_attribute
		where attrelid = relation::regclass and attnum = colnum
		into result;
		
		if result is null then
			raise exception 'attribute #% of relation % not found',
							colnum, quote_literal(relation);
		end if;

		return result;
	END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION to_namespace(nsp text) RETURNS OID AS $$
	DECLARE
		result oid;
		
    BEGIN
		select oid
		from pg_catalog.pg_namespace
		where nspname = nsp
		into result;
		
		if result is null then
			raise exception 'schema % does not exist',
							quote_literal(nsp);
		end if;

		return result;
	END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION get_namespace(relation oid) RETURNS OID AS $$
	DECLARE
		result oid;
		
    BEGIN
		select relnamespace
		from pg_catalog.pg_class
		where oid = relation
		into result;
		
		if result is null then
			raise exception 'relation % does not exist', relation;
		end if;

		return result;
	END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION dump_statistic_ext(relid oid) RETURNS SETOF TEXT AS $$
	DECLARE
		result	text;
		r		record;
		cmd		text;
		
		relname	text;
		qualname text;
		statcols text;
		statkinds text;
		exprs text;
		
	BEGIN
		for r in
			SELECT se.oid, se.stxname, n.nspname, 
				   array_agg(att.attname ORDER BY u.attnum) as attnames,
				   array_agg(att.attnum ORDER BY u.attnum) as attnums,
				   se.stxkind,
				   sed.stxdinherit,
				   sed.stxdndistinct,
				   sed.stxddependencies,
				   sed.stxdmcv
			FROM pg_statistic_ext se
			JOIN pg_namespace n ON n.oid = se.stxnamespace
			JOIN LATERAL unnest(se.stxkeys) WITH ORDINALITY AS u(attnum, ord) ON true
			JOIN pg_attribute att ON att.attrelid = se.stxrelid AND att.attnum = u.attnum
			LEFT JOIN pg_statistic_ext_data sed ON sed.stxoid = se.oid
			WHERE se.stxrelid = relid
				AND n.nspname NOT IN ('information_schema', 'pg_catalog')
			GROUP BY se.oid, se.stxname, n.nspname, se.stxkind, 
					 sed.stxdinherit, sed.stxdndistinct, 
					 sed.stxddependencies, sed.stxdmcv
		loop
			
			relname := to_schema_qualified_relation(relid);
			qualname := quote_ident(r.nspname) || '.' || quote_ident(r.stxname);
			
			statcols := '';
			FOR i IN 1..array_length(r.attnames, 1) LOOP
				IF i > 1 THEN
					statcols := statcols || ', ';
				END IF;
				statcols := statcols || quote_ident(r.attnames[i]);
			END LOOP;
			
			statkinds := '''{'';
			FOR i IN 1..array_length(r.stxkind, 1) LOOP
				IF i > 1 THEN
					statkinds := statkinds || '', '';
				END IF;
				statkinds := statkinds || quote_literal(r.stxkind[i]);
			END LOOP;
			statkinds := statkinds || ''}''::char[]';
			
			result := format('CREATE STATISTICS %s ON %s FROM %s;',
						   qualname, statcols, relname);
			return next result;
			
			IF r.stxdndistinct IS NOT NULL OR r.stxddependencies IS NOT NULL OR r.stxdmcv IS NOT NULL THEN
				cmd := 'INSERT INTO pg_statistic_ext_data (stxoid, stxdinherit, stxdndistinct, stxddependencies, stxdmcv) VALUES (';
				cmd := cmd || format('''%s''::regclass, ', qualname);
				cmd := cmd || COALESCE(r.stxdinherit::text, 'NULL') || ', ';
				cmd := cmd || COALESCE(quote_literal(r.stxdndistinct::text) || '::pg_ndistinct', 'NULL') || ', ';
				cmd := cmd || COALESCE(quote_literal(r.stxddependencies::text) || '::pg_dependencies', 'NULL') || ', ';
				cmd := cmd || COALESCE(quote_literal(r.stxdmcv::text) || '::pg_mcv_list', 'NULL') || ');';
				
				return next cmd;
			END IF;
			
		end loop;
		
		return;
	END;
$$ LANGUAGE plpgsql;



CREATE FUNCTION dump_statistic_ext() RETURNS SETOF TEXT AS $$
	DECLARE
		relid	oid;
		i		text;
		
	BEGIN
		for relid in
			SELECT pg_class.oid
			FROM pg_namespace
			INNER JOIN pg_class ON relnamespace = pg_namespace.oid
			WHERE nspname NOT IN ('information_schema', 'pg_catalog')
		loop
			for i in select dump_statistic_ext(relid) loop
				return next i;
			end loop;
		end loop;
		
		return;
	END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION dump_statistic(relid oid) RETURNS SETOF TEXT AS $$
	DECLARE
		result	text;
		
		cmd		text;		-- main query
		in_args	text;		-- args for insert
		up_args	text;		-- args for upsert
		
		fstaop	text := '%s::regoperator';
		arr_in	text := 'array_in(%s, %s::regtype, -1)::anyarray';
		
		stacols text[] = ARRAY['stakind', 'staop', 'stacoll',
							   'stanumbers', 'stavalues' ];
		
		r		record;
		i		int;
		j		text;
		ncols	int := 31;	-- number of columns in pg_statistic
		
		stanum	text[];		-- stanumbers{1, 5}
		staval	text[];		-- stavalues{1, 5}
		staop	text[];		-- staop{1, 5}
		stacoll text[];

		relname	text;		-- quoted relation name
		attname	text;		-- quoted attribute name
		atttype text;		-- quoted attribute type
		
	BEGIN
		for r in
				select * from pg_catalog.pg_statistic
				where starelid = relid
					and get_namespace(starelid) != to_namespace('information_schema')
					and get_namespace(starelid) != to_namespace('pg_catalog') loop
			
			relname := to_schema_qualified_relation(r.starelid);
			attname := quote_literal(to_attname(relname, r.staattnum));
			atttype := quote_literal(to_atttype(relname, r.staattnum));
			relname := quote_literal(relname); -- redefine relname
			
			in_args := '';
			up_args = 'stanullfrac = %s, stawidth = %s, stadistinct = %s, ';
			
			cmd := 'WITH upsert as ( ' ||
						'UPDATE pg_catalog.pg_statistic SET %s ' ||
						'WHERE to_schema_qualified_relation(starelid) = ' || relname || ' '
							'AND to_attname(' || relname || ', staattnum) = ' || attname || ' '
							'AND to_atttype(' || relname || ', staattnum) = ' || atttype || ' '
							'AND stainherit = ' || r.stainherit || ' ' ||
						'RETURNING *), ' ||
				   'ins as ( ' ||
						'SELECT %s ' ||
						'WHERE NOT EXISTS (SELECT * FROM upsert) ' ||
							'AND to_attnum(' || relname || ', ' || attname || ') IS NOT NULL '
							'AND to_atttype(' || relname || ', ' || attname || ') = ' || atttype || ') '
				   'INSERT INTO pg_catalog.pg_statistic SELECT * FROM ins;';
					
			for i in 1..ncols loop
				in_args := in_args || '%s';

				if i != ncols then
					in_args := in_args || ', ';
				end if;
			end loop;
				
			for j in 1..5 loop
				for i in 1..5 loop
					up_args := up_args || format('%s%s = %%s', stacols[j], i);

					if i * j != 25 then
						up_args := up_args || ', ';
					end if;
				end loop;
			end loop;
			
			cmd := format(cmd, up_args, in_args);	--prepare template for main query

			staop := array[format(fstaop, quote_literal(to_schema_qualified_operator(r.staop1))),
						   format(fstaop, quote_literal(to_schema_qualified_operator(r.staop2))),
						   format(fstaop, quote_literal(to_schema_qualified_operator(r.staop3))),
						   format(fstaop, quote_literal(to_schema_qualified_operator(r.staop4))),
						   format(fstaop, quote_literal(to_schema_qualified_operator(r.staop5)))];

			stacoll := array[r.stacoll1::text,
							r.stacoll2::text,
							r.stacoll3::text,
							r.stacoll4::text,
							r.stacoll5::text];

			
			stanum := array[r.stanumbers1::text,
							r.stanumbers2::text,
							r.stanumbers3::text,
							r.stanumbers4::text,
							r.stanumbers5::text];

			
							
			for i in 1..5 loop
				if stanum[i] is null then
					stanum[i] := 'NULL::real[]';
				else
					stanum[i] := '''' || stanum[i] || '''::real[]';
				end if;
			end loop;

			if r.stavalues1 is not null then
				staval[1] := format(arr_in, quote_literal(r.stavalues1),
									quote_literal(
										to_schema_qualified_type(
											anyarray_elemtype(r.stavalues1))));
			else
				staval[1] := 'NULL::anyarray';
			end if;

			if r.stavalues2 is not null then
				staval[2] := format(arr_in, quote_literal(r.stavalues2),
									quote_literal(
										to_schema_qualified_type(
											anyarray_elemtype(r.stavalues2))));
			else
				staval[2] := 'NULL::anyarray';
			end if;

			if r.stavalues3 is not null then
				staval[3] := format(arr_in, quote_literal(r.stavalues3),
									quote_literal(
										to_schema_qualified_type(
											anyarray_elemtype(r.stavalues3))));
			else
				staval[3] := 'NULL::anyarray';
			end if;

			if r.stavalues4 is not null then
				staval[4] := format(arr_in, quote_literal(r.stavalues4),
									quote_literal(
										to_schema_qualified_type(
											anyarray_elemtype(r.stavalues4))));
			else
				staval[4] := 'NULL::anyarray';
			end if;

			if r.stavalues5 is not null then
				staval[5] := format(arr_in, quote_literal(r.stavalues5),
									quote_literal(
										to_schema_qualified_type(
											anyarray_elemtype(r.stavalues5))));
			else
				staval[5] := 'NULL::anyarray';
			end if;
			
			--DEBUG
			--staop := array['{arr}', '{arr}', '{arr}', '{arr}', '{arr}'];
			--stanum := array['{num}', '{num}', '{num}', '{num}', '{num}'];
			--staval := array['{val}', '{val}', '{val}', '{val}', '{val}'];

			result := format(cmd,
							 r.stanullfrac,
							 r.stawidth,
							 r.stadistinct,
							 -- stakind
							 r.stakind1, r.stakind2, r.stakind3, r.stakind4, r.stakind5,
							 -- staop
							 staop[1], staop[2], staop[3], staop[4], staop[5],
							 -- stacolls
							 stacoll[1], stacoll[2], stacoll[3], stacoll[4], stacoll[5],
							 -- stanumbers
							 stanum[1], stanum[2], stanum[3], stanum[4], stanum[5],
							 -- stavalues
							 staval[1], staval[2], staval[3], staval[4], staval[5],
							 
							 -- first 6 columns
							 format('%s::regclass', relname),
							 format('to_attnum(%s, %s)', relname, attname),
							 '''' || r.stainherit || '''::boolean',
							 r.stanullfrac || '::real',
							 r.stawidth || '::integer',
							 r.stadistinct || '::real',
							 -- stakind
							 r.stakind1, r.stakind2, r.stakind3, r.stakind4, r.stakind5,
							 -- staop
							 staop[1], staop[2], staop[3], staop[4], staop[5],
							 -- stacoll
							 stacoll[1], stacoll[2], stacoll[3], stacoll[4], stacoll[5],
							 -- stanumbers
							 stanum[1], stanum[2], stanum[3], stanum[4], stanum[5],
							 -- stavalues
							 staval[1], staval[2], staval[3], staval[4], staval[5]);

			return next result;
		end loop;

		return;
	END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION dump_statistic(schema_name text, table_name text) RETURNS SETOF TEXT AS $$
	DECLARE
		qual_relname	text;
		relid			oid;
		i				text;

	BEGIN
		qual_relname := quote_ident(schema_name) ||
							'.' || quote_ident(table_name);
	
		for i in select dump_statistic(qual_relname::regclass) loop
			return next i;
		end loop;
		
		return;
		
	EXCEPTION
		when invalid_schema_name then
			raise exception 'schema % does not exist',
							quote_literal(schema_name);
		when undefined_table then
			raise exception 'relation % does not exist',
							quote_literal(qual_relname);
	END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION dump_statistic(schema_name text) RETURNS SETOF TEXT AS $$
	DECLARE
		relid	oid;
		i		text;
		
	BEGIN
		perform to_namespace(schema_name);
	
		for relid in
				select pg_class.oid
				from pg_catalog.pg_namespace
					inner join pg_catalog.pg_class
					on relnamespace = pg_namespace.oid
				where nspname = schema_name loop
			
			for i in select dump_statistic(relid) loop
				return next i;
			end loop;
		end loop;
		
		return;
		
	EXCEPTION
		when invalid_schema_name then
			raise exception 'schema % does not exist',
							quote_literal(schema_name);
	END;
$$ LANGUAGE plpgsql;


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
    mcv_frequencies text,
    avg_column_width_bytes integer
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
        (SELECT attname FROM pg_attribute 
         WHERE attrelid = con.confrelid AND attnum = con.confkey[gs.pos]) AS referenced_column
    FROM pg_constraint con
    JOIN LATERAL generate_subscripts(con.conkey, 1) AS gs(pos) ON true
    JOIN pg_attribute a ON a.attrelid = con.conrelid AND a.attnum = con.conkey[gs.pos]
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
        BOOL_OR(c.contype = 'u') AS is_unique_constraint
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
            WHEN cc.is_pk OR cc.is_unique_constraint THEN 'ONE_TO_ONE'
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
         WHERE conrelid = c.oid AND contype = 'u' AND a.attnum = ANY(conkey)) AS is_unique
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
        CASE WHEN cs.is_unique > 0 THEN 'UNIQUE' ELSE '' END
    ) AS modifiers,
    cs.max_length,
    rt.relation_type,
    rt.referenced_table,
    rt.referenced_column,
    s.most_common_vals AS mcv,
    s.most_common_freqs AS mcv_frequencies,
    s.avg_width AS avg_column_width_bytes
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




CREATE FUNCTION dump_all_statistics(relid oid) RETURNS SETOF TEXT AS $$
	DECLARE
		i text;
	BEGIN
		FOR i IN SELECT dump_statistic(relid) LOOP
			RETURN NEXT i;
		END LOOP;
		
		FOR i IN SELECT dump_statistic_ext(relid) LOOP
			RETURN NEXT i;
		END LOOP;
		
		RETURN;
	END;
$$ LANGUAGE plpgsql;





CREATE FUNCTION dump_all_statistics() RETURNS SETOF TEXT AS $$
	DECLARE
		i text;
	BEGIN
		FOR i IN SELECT dump_statistic() LOOP
			RETURN NEXT i;
		END LOOP;
		
		FOR i IN SELECT dump_statistic_ext() LOOP
			RETURN NEXT i;
		END LOOP;
		
		RETURN;
	END;
$$ LANGUAGE plpgsql;