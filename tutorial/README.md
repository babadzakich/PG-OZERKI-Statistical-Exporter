# Туториал для pg_ozerki

## Шаг 1
Создайте исходную базу данных ***PostgreSQL***, например через **psql**:
```sql
CREATE DATABASE src;
```

## Шаг 2
Выполните в ней инициализирующий скрипт:
```bash
psql -f tutorial_init.sql src
```

## Шаг 3
Создайте конфигурацию для экспорта по образцу из `export_config.yaml`, например:
```yaml
dbname: src
username: postgres
host: localhost
port: 5432
schema-file: src_schema.sql
stats-file: src_stats.csv
explainfile: src_explain.json
query-file: src_query.sql
constr-file: src_constr.csv
index-file: src_indexes.sql
no-checks:
```

## Шаг 4
Запустите приложение экспорта
```bash
./pg_ozerki export_config.yaml
```
## Шаг 5
Создайте аналогично *Шагу 1* тестовую БД, например:
```sql
CREATE DATABASE new;
```

## Шаг 6
Создайте конфигурацию для приложения генерации данных по образцу из `datagen_config.yaml`:
```yaml
host: localhost
port: 5432
user: postgres
password: postgres
dbname: new
schemaPath: src_schema.sql
statPath: src_stats.csv
indexFile: src_indexes.sql
constraintFile:  src_constr.csv
batchSize: 50000
generationThreadPoolSize: 4
globStoreThreads: 1
tableStoreThreads: 1
```

## Шаг 7
Соберите приложение генерации данных:
```bash
./gradlew build
```

## Шаг 8
Запустите приложение генерации данных:
```bash
java -jar ./build/libs/datagen-1.0-SNAPSHOT.jar -config datagen_config.yaml
```

# Проблемный запрос

Проблемный запрос выглядит так:
```sql
SELECT count(distinct id) FROM users;
```
Оптимизированный запрос:
```sql
SELECT count(id) FROM users;
```

Ввиду особенностей планировщика postgres, при выполнении проблемного запроса из-за `distinct` не используется параллельная обработка, из-за чего запрос работает дольше (см. **Execution Time**)

```
new=# explain analyze select count(distinct id) from users;
                                                                  QUERY PLAN

----------------------------------------------------------------------------------------------------------------------------
-------------------
 Aggregate  (cost=35967.82..35967.83 rows=1 width=8) (actual time=332.568..332.570 rows=1 loops=1)
   ->  Index Only Scan using users_pkey on users  (cost=0.42..33467.82 rows=1000000 width=8) (actual time=0.569..210.654 row
s=1000000 loops=1)
         Heap Fetches: 0
 Planning Time: 0.369 ms
 Execution Time: 332.733 ms
(5 rows)

new=# explain analyze select count(id) from users;
                                                               QUERY PLAN

----------------------------------------------------------------------------------------------------------------------------
-------------
 Finalize Aggregate  (cost=22695.55..22695.56 rows=1 width=8) (actual time=107.301..113.243 rows=1 loops=1)
   ->  Gather  (cost=22695.33..22695.54 rows=2 width=8) (actual time=106.801..113.222 rows=3 loops=1)
         Workers Planned: 2
         Workers Launched: 2
         ->  Partial Aggregate  (cost=21695.33..21695.34 rows=1 width=8) (actual time=93.637..93.639 rows=1 loops=3)
               ->  Parallel Seq Scan on users  (cost=0.00..20653.67 rows=416667 width=8) (actual time=0.056..50.151 rows=333
333 loops=3)
 Planning Time: 0.430 ms
 Execution Time: 113.463 ms
(8 rows)
```