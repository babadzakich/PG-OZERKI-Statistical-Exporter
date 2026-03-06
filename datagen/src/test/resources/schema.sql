--PostgreSQL database schema dump by PG_OZERKI
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
--
-- Planner Settings
--

ALTER SYSTEM SET cpu_index_tuple_cost = 0.005;
ALTER SYSTEM SET cpu_tuple_cost = 0.01;
ALTER SYSTEM SET default_statistics_target = 100;
ALTER SYSTEM SET effective_cache_size = '6553618kB';
ALTER SYSTEM SET random_page_cost = 4;
ALTER SYSTEM SET seq_page_cost = 1;
ALTER SYSTEM SET work_mem = '4096kB';

--
-- Schemas
--

--
-- Extensions
--

CREATE EXTENSION IF NOT EXISTS plpgsql WITH SCHEMA pg_catalog VERSION '1.0';
COMMENT ON EXTENSION plpgsql IS 'PL/pgSQL procedural language';

--
-- Tables
--

CREATE TABLE public.meta (
    id integer NOT NULL,
    product_review_id integer,
    comment text,
    CONSTRAINT meta_pkey PRIMARY KEY (id)
);



CREATE TABLE public.product_review (
    id integer NOT NULL,
    user_id integer,
    product_id integer,
    CONSTRAINT product_review_pkey PRIMARY KEY (id)
);



--
-- Sequences
--

CREATE SEQUENCE public.meta_id_seq
    INCREMENT BY 1
    MINVALUE 1
    MAXVALUE 2147483647
    NO CYCLE;
ALTER SEQUENCE public.meta_id_seq OWNED BY public.meta.id;
SELECT pg_catalog.setval('public.meta_id_seq', 500000, false);

--
-- Constraints
--

ALTER TABLE ONLY public.meta
    ADD CONSTRAINT fk_prod_rew FOREIGN KEY (product_review_id) REFERENCES public.product_review(id) ON DELETE CASCADE;

ALTER TABLE ONLY public.product_review
    ADD CONSTRAINT product_review_user_id_product_id_key UNIQUE (user_id, product_id);


