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
ALTER SYSTEM SET parallel_tuple_cost = 0.1;
ALTER SYSTEM SET parallel_setup_cost = 1000;
ALTER SYSTEM SET jit = off;
SET client_min_messages = warning;
SET row_security = off;

SELECT pg_reload_conf();
--
-- Name: bookings; Type: SCHEMA; Schema: -; Owner: -
--

CREATE SCHEMA bookings;


SET search_path = bookings, pg_catalog;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: airports_data; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.airports_data (
    airport_code character(3) NOT NULL,
    timezone text NOT NULL
);


--
-- Name: flights; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.flights (
    flight_id integer NOT NULL,
    route_no text NOT NULL,
    status text NOT NULL,
    scheduled_departure timestamp with time zone NOT NULL,
    scheduled_arrival timestamp with time zone NOT NULL,
    actual_departure timestamp with time zone,
    actual_arrival timestamp with time zone
);


--
-- Name: routes; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.routes (
    route_no text NOT NULL,
    departure_airport character(3) NOT NULL,
    arrival_airport character(3) NOT NULL,
    airplane_code character(3) NOT NULL,
    days_of_week integer[] NOT NULL,
    scheduled_time time without time zone NOT NULL,
    duration interval NOT NULL
);


--
-- Name: segments; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.segments (
    ticket_no text NOT NULL,
    flight_id integer NOT NULL,
    fare_conditions text NOT NULL,
    price numeric(10,2) NOT NULL
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


--
-- Name: airports_data airports_data_pkey; Type: CONSTRAINT; Schema: bookings; Owner: -
--



--
-- PostgreSQL database dump complete
--

ALTER TABLE ONLY bookings.airports_data
    ADD CONSTRAINT airports_data_pkey PRIMARY KEY (airport_code);


--
-- Name: flights flights_pkey; Type: CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.flights
    ADD CONSTRAINT flights_pkey PRIMARY KEY (flight_id);


--
-- Name: flights flights_route_no_scheduled_departure_key; Type: CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.flights
    ADD CONSTRAINT flights_route_no_scheduled_departure_key UNIQUE (route_no, scheduled_departure);


--
-- Name: segments segments_pkey; Type: CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.segments
    ADD CONSTRAINT segments_pkey PRIMARY KEY (ticket_no, flight_id);


ALTER TABLE ONLY bookings.routes
    ADD CONSTRAINT routes_airplane_code_fkey FOREIGN KEY (airplane_code) REFERENCES airplanes_data(airplane_code);


--
-- Name: routes routes_arrival_airport_fkey; Type: FK CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.routes
    ADD CONSTRAINT routes_arrival_airport_fkey FOREIGN KEY (arrival_airport) REFERENCES airports_data(airport_code);


--
-- Name: routes routes_departure_airport_fkey; Type: FK CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.routes
    ADD CONSTRAINT routes_departure_airport_fkey FOREIGN KEY (departure_airport) REFERENCES airports_data(airport_code);


--
-- Name: segments segments_flight_id_fkey; Type: FK CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.segments
    ADD CONSTRAINT segments_flight_id_fkey FOREIGN KEY (flight_id) REFERENCES flights(flight_id);


--
-- Name: segments segments_ticket_no_fkey; Type: FK CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.segments
    ADD CONSTRAINT segments_ticket_no_fkey FOREIGN KEY (ticket_no) REFERENCES tickets(ticket_no);
