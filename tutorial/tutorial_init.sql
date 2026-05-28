CREATE TABLE users
(
id bigint primary key,
login varchar(200) not null,
first_name varchar(200) not null,
last_name varchar(200) not null,
create_date timestamp not null default now()
);
INSERT INTO users
SELECT id, random() * id, md5(sin(id)::text),
md5(cos(id)::text)
FROM generate_series(1, 1000000) id;
ANALYZE users;
