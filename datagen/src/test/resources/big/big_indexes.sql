
--
-- Name: routes_departure_airport_lower_idx; Type: INDEX; Schema: bookings; Owner: -
--



--
-- Name: segments_flight_id_idx; Type: INDEX; Schema: bookings; Owner: -
--

CREATE INDEX segments_flight_id_idx ON bookings.segments USING btree (flight_id);


--
-- Name: routes routes_airplane_code_fkey; Type: FK CONSTRAINT; Schema: bookings; Owner: -
--
