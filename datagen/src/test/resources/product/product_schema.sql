--
-- PostgreSQL database dump
--

-- Dumped from database version 17.5
-- Dumped by pg_ozerki version 17.5

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET transaction_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SET check_function_bodies = false;
SET xmloption = content;
ALTER SYSTEM SET cpu_index_tuple_cost = 0.005;
ALTER SYSTEM SET cpu_tuple_cost = 0.01;
ALTER SYSTEM SET default_statistics_target = 100;
ALTER SYSTEM SET effective_cache_size = '8192028kB';
ALTER SYSTEM SET random_page_cost = 4;
ALTER SYSTEM SET seq_page_cost = 1;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: pg_ozerki; Type: EXTENSION; Schema: -; Owner: -
--



SET search_path = public, pg_catalog;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: meta; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.meta (
    id integer NOT NULL,
    review_code integer NOT NULL,
    comment text
);


--
-- Name: product_review; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.product_review (
    id integer NOT NULL,
    review_code integer NOT NULL,
    user_id integer,
    product_id integer
);


--
-- Name: meta meta_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.meta
    ADD CONSTRAINT meta_pkey PRIMARY KEY (id);


--
-- Name: product_review product_review_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.product_review
    ADD CONSTRAINT product_review_pkey PRIMARY KEY (id);


--
-- Name: product_review product_review_review_code_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.product_review
    ADD CONSTRAINT product_review_review_code_key UNIQUE (review_code);


--
-- Name: product_review product_review_user_id_product_id_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.product_review
    ADD CONSTRAINT product_review_user_id_product_id_key UNIQUE (user_id, product_id);


--
-- Name: meta fk_prod_rew; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.meta
    ADD CONSTRAINT fk_prod_rew FOREIGN KEY (review_code) REFERENCES product_review(review_code) ON DELETE CASCADE;


--
-- PostgreSQL database dump complete
--

