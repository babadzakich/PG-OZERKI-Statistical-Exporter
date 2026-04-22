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
ALTER SYSTEM SET effective_cache_size = '6553618kB';
ALTER SYSTEM SET random_page_cost = 4;
ALTER SYSTEM SET seq_page_cost = 1;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: bookings; Type: SCHEMA; Schema: -; Owner: -
--

CREATE SCHEMA bookings;


--
-- Name: pg_ozerki; Type: EXTENSION; Schema: -; Owner: -
--

--CREATE EXTENSION IF NOT EXISTS pg_ozerki WITH SCHEMA public;


SET search_path = bookings, pg_catalog;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: airports_data; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.airports_data (
    airport_code character(3) ,
    timezone text
);


--
-- Name: flights; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.flights (
    flight_id integer NOT NULL ,
    route_no text ,
    status text ,
    scheduled_departure timestamp with time zone ,
    scheduled_arrival timestamp with time zone ,
    actual_departure timestamp with time zone,
    actual_arrival timestamp with time zone
);


--
-- Name: flights_flight_id_seq; Type: SEQUENCE; Schema: bookings; Owner: -
--

-- ALTER TABLE bookings.flights ALTER COLUMN flight_id ADD GENERATED ALWAYS AS IDENTITY (
--     SEQUENCE NAME bookings.flights_flight_id_seq
--     START WITH 1
--     INCREMENT BY 1
--     NO MINVALUE
--     NO MAXVALUE
--     CACHE 1
-- );


--
-- Name: routes; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.routes (
    route_no text ,
    departure_airport character(3) ,
    arrival_airport character(3) ,
    airplane_code character(3) ,
    days_of_week integer[] ,
    scheduled_time time without time zone ,
    duration interval
);


--
-- Name: timetable; Type: VIEW; Schema: bookings; Owner: -
--

CREATE VIEW bookings.timetable AS
 SELECT f.flight_id,
    f.route_no,
    r.departure_airport,
    r.arrival_airport,
    f.status,
    r.airplane_code,
    f.scheduled_departure,
    (f.scheduled_departure AT TIME ZONE dep.timezone) AS scheduled_departure_local,
    f.actual_departure,
    (f.actual_departure AT TIME ZONE dep.timezone) AS actual_departure_local,
    f.scheduled_arrival,
    (f.scheduled_arrival AT TIME ZONE arr.timezone) AS scheduled_arrival_local,
    f.actual_arrival,
    (f.actual_arrival AT TIME ZONE arr.timezone) AS actual_arrival_local
   FROM (((flights f
     JOIN routes r ON (((r.route_no = f.route_no))))
     JOIN airports_data dep ON ((dep.airport_code = r.departure_airport)))
     JOIN airports_data arr ON ((arr.airport_code = r.arrival_airport)));



