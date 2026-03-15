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