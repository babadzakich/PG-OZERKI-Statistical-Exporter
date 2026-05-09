-- === INDEXES ===

CREATE INDEX routes_departure_airport_lower_idx ON bookings.routes USING btree (departure_airport, lower(validity));


CREATE INDEX segments_flight_id_idx ON bookings.segments USING btree (flight_id);


