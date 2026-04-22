--
-- Name: airports_data airports_data_pkey; Type: CONSTRAINT; Schema: bookings; Owner: -
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
-- Name: routes_departure_airport_lower_idx; Type: INDEX; Schema: bookings; Owner: -
--

CREATE INDEX routes_departure_airport_lower_idx ON bookings.routes USING btree (departure_airport);


--
-- Name: routes routes_airplane_code_fkey; Type: FK CONSTRAINT; Schema: bookings; Owner: -
--

--ALTER TABLE ONLY bookings.routes
--    ADD CONSTRAINT routes_airplane_code_fkey FOREIGN KEY (airplane_code) REFERENCES airplanes_data(airplane_code);


--
-- Name: routes routes_arrival_airport_fkey; Type: FK CONSTRAINT; Schema: bookings; Owner: -
--
--
-- PostgreSQL database dump complete
--
