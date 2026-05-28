-- === INDEXES ===

CREATE INDEX segments_flight_id_idx ON bookings.segments USING btree (flight_id);


