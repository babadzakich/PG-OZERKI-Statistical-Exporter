select * from flights f
join segments s on f.flight_id = s.flight_id
where f.status = 'Arrived'