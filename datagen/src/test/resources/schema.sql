--PostgreSQL database schema dump by PG_OZERKI
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
--
-- Schemas
--

--
-- Extensions
--

CREATE EXTENSION IF NOT EXISTS pg_ozerki WITH SCHEMA public VERSION '1.0';
COMMENT ON EXTENSION pg_ozerki IS 'pg_ozerki extension';

CREATE EXTENSION IF NOT EXISTS plpgsql WITH SCHEMA pg_catalog VERSION '1.0';
COMMENT ON EXTENSION plpgsql IS 'PL/pgSQL procedural language';

--
-- Tables
--

CREATE TABLE public.product_review (
    id integer NOT NULL,
    user_id integer,
    product_id integer,
    CONSTRAINT product_review_pkey PRIMARY KEY (id)
);



--
-- Sequences
--

CREATE SEQUENCE public.product_review_id_seq
    INCREMENT BY 1
    MINVALUE 1
    MAXVALUE 2147483647
    NO CYCLE;
ALTER SEQUENCE public.product_review_id_seq OWNED BY public.product_review.id;
SELECT pg_catalog.setval('public.product_review_id_seq', 500000, false);

--
-- Constraints
--

ALTER TABLE ONLY public.product_review
    ADD CONSTRAINT product_review_user_id_product_id_key UNIQUE (user_id, product_id);


