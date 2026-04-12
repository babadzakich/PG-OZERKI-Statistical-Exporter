--
-- Name: airplanes_data airplanes_data_pkey; Type: CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.airplanes_data
    ADD CONSTRAINT airplanes_data_pkey PRIMARY KEY (airplane_code);


--
-- Name: airports_data airports_data_pkey; Type: CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.airports_data
    ADD CONSTRAINT airports_data_pkey PRIMARY KEY (airport_code);


--
-- Name: bookings bookings_pkey; Type: CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.bookings
    ADD CONSTRAINT bookings_pkey PRIMARY KEY (book_ref);


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
-- Name: seats seats_pkey; Type: CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.seats
    ADD CONSTRAINT seats_pkey PRIMARY KEY (airplane_code, seat_no);


--
-- Name: segments segments_pkey; Type: CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.segments
    ADD CONSTRAINT segments_pkey PRIMARY KEY (ticket_no, flight_id);


--
-- Name: tickets tickets_book_ref_passenger_id_outbound_key; Type: CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.tickets
    ADD CONSTRAINT tickets_book_ref_passenger_id_outbound_key UNIQUE (book_ref, passenger_id, outbound);


--
-- Name: tickets tickets_pkey; Type: CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.tickets
    ADD CONSTRAINT tickets_pkey PRIMARY KEY (ticket_no);


--
-- Name: segments_flight_id_idx; Type: INDEX; Schema: bookings; Owner: -
--

CREATE INDEX segments_flight_id_idx ON bookings.segments USING btree (flight_id);


--
-- Name: routes routes_airplane_code_fkey; Type: FK CONSTRAINT; Schema: bookings; Owner: -
--

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
-- Name: seats seats_airplane_code_fkey; Type: FK CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.seats
    ADD CONSTRAINT seats_airplane_code_fkey FOREIGN KEY (airplane_code) REFERENCES airplanes_data(airplane_code) ON DELETE CASCADE;


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


--
-- Name: tickets tickets_book_ref_fkey; Type: FK CONSTRAINT; Schema: bookings; Owner: -
--

ALTER TABLE ONLY bookings.tickets
    ADD CONSTRAINT tickets_book_ref_fkey FOREIGN KEY (book_ref) REFERENCES bookings(book_ref);


--
-- PostgreSQL database dump complete
--
