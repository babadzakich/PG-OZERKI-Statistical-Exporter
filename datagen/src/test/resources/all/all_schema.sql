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
-- Name: bookings; Type: SCHEMA; Schema: -; Owner: -
--

CREATE SCHEMA bookings;


SET search_path = bookings, pg_catalog;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: airplanes_data; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.airplanes_data (
    airplane_code character(3) NOT NULL,
    range integer NOT NULL,
    speed integer NOT NULL
);


--
-- Name: airports_data; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.airports_data (
    airport_code character(3) NOT NULL,
    timezone text NOT NULL
);


--
-- Name: bookings; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.bookings (
    book_ref character(6) NOT NULL,
    book_date timestamp with time zone NOT NULL,
    total_amount numeric(10,2) NOT NULL
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
-- Name: seats; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.seats (
    airplane_code character(3) NOT NULL,
    seat_no text NOT NULL,
    fare_conditions text NOT NULL
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
-- Name: tickets; Type: TABLE; Schema: bookings; Owner: -
--

CREATE TABLE bookings.tickets (
    ticket_no text NOT NULL,
    book_ref character(6) NOT NULL,
    passenger_id text NOT NULL,
    passenger_name text NOT NULL,
    outbound boolean NOT NULL
);


--
-- PostgreSQL database dump complete
--

ALTER TABLE ONLY bookings.airplanes_data
    ADD CONSTRAINT airplanes_data_pkey PRIMARY KEY (airplane_code);


ALTER TABLE ONLY bookings.airports_data
    ADD CONSTRAINT airports_data_pkey PRIMARY KEY (airport_code);


ALTER TABLE ONLY bookings.bookings
    ADD CONSTRAINT bookings_pkey PRIMARY KEY (book_ref);


ALTER TABLE ONLY bookings.flights
    ADD CONSTRAINT flights_pkey PRIMARY KEY (flight_id);


ALTER TABLE ONLY bookings.flights
    ADD CONSTRAINT flights_route_no_scheduled_departure_key UNIQUE (route_no, scheduled_departure);


ALTER TABLE ONLY bookings.seats
    ADD CONSTRAINT seats_pkey PRIMARY KEY (airplane_code, seat_no);


ALTER TABLE ONLY bookings.segments
    ADD CONSTRAINT segments_pkey PRIMARY KEY (ticket_no, flight_id);


ALTER TABLE ONLY bookings.tickets
    ADD CONSTRAINT tickets_book_ref_passenger_id_outbound_key UNIQUE (book_ref, passenger_id, outbound);


ALTER TABLE ONLY bookings.tickets
    ADD CONSTRAINT tickets_pkey PRIMARY KEY (ticket_no);


ALTER TABLE ONLY bookings.routes
    ADD CONSTRAINT routes_airplane_code_fkey FOREIGN KEY (airplane_code) REFERENCES airplanes_data(airplane_code);


ALTER TABLE ONLY bookings.routes
    ADD CONSTRAINT routes_arrival_airport_fkey FOREIGN KEY (arrival_airport) REFERENCES airports_data(airport_code);


ALTER TABLE ONLY bookings.routes
    ADD CONSTRAINT routes_departure_airport_fkey FOREIGN KEY (departure_airport) REFERENCES airports_data(airport_code);


ALTER TABLE ONLY bookings.seats
    ADD CONSTRAINT seats_airplane_code_fkey FOREIGN KEY (airplane_code) REFERENCES airplanes_data(airplane_code) ON DELETE CASCADE;


ALTER TABLE ONLY bookings.segments
    ADD CONSTRAINT segments_flight_id_fkey FOREIGN KEY (flight_id) REFERENCES flights(flight_id);


ALTER TABLE ONLY bookings.segments
    ADD CONSTRAINT segments_ticket_no_fkey FOREIGN KEY (ticket_no) REFERENCES tickets(ticket_no);


ALTER TABLE ONLY bookings.tickets
    ADD CONSTRAINT tickets_book_ref_fkey FOREIGN KEY (book_ref) REFERENCES bookings(book_ref);


