# PG_OZERKI Statistical Exporter
*Решение по генерации тестовых данных на основе статистики для **PostgreSQL***

## Структура
Состоит из двух частей:

* Приложение экспорта, которое экспортирует схему и статистику БД
* Приложение генерации данных



# Приложение экспорта


Утилита для эскпорта в проекте **PG OZERKI STATISTICAL EXPORTER**


## Сборка

1. Перенести папку **pg_ozerki**  в **src/bin/** в дереве исходников **PostgreSQL**
2. Из неё запустить 
    ```bash
    make install
    ```

## Использование

Запуск:
```bash
./pg_ozerki <путь_к_файлу_конфига.yaml>
```

Где поля в .yaml конфиге:

* **dbname** - имя БД
* **username** - имя пользователя
* **host** - имя хоста
* **port** - порт
* **schema-file** - имя файла, в который экспортируется схема (.sql)
* **stats-file** - имя файла, в который экспортируется статистика (.csv)
* **query** - текст запроса в кавычках, по которому будет выполняться экспорт
* **explainfile** - имя файла, куда будет экспортироваться результат EXPLAIN в формате json для запроса
* **explainfile-analyze** - имя файла, куда будет экспортироваться результат EXPLAIN ANALYZE в формате json для запроса
* **no-checks** - при установке этого флага не экспортируются CHECK ограничения (поле без аргумента)
* **no-exts** - при установке этого флага не экспортируются расширения (поле без аргумента)
* **query-file** - имя файла, содержащего текст запроса (нельзя использовать одновременно с флагом **--query**)
* **constr-file** - имя файла, куда будет экспортироваться информация об ограничениях (.csv) (***обязателен***)
* **constr-file** - имя файла, куда будут экспортироваться нестрогие индексы (.sql) (***обязателен***)
Например:
```bash
./pg_ozerki export_config.yaml
```

Где `export_config.yaml` - это, например

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

# Приложение генерации данных

## Сборка

```bash
./gradlew build
```

## Запуск

```bash
java -jar ./build/libs/datagen-1.0-SNAPSHOT.jar -config <путь_к_файлу_конфига.yaml>
```

Где поля в .yaml конфиге:

* **dbname** - имя БД
* **username** - имя пользователя
* **host** - имя хоста
* **port** - порт
* **password** - пароль
* **schemaPath** - путь к .sql-файлу с схемой БД
* **statPath** - путь к .csv-файлу с статистикой БД
* **indexFile** - путь к .sql-файлу с нестрогими индексами
* **constraintFile** - путь к .csv-файлу с информацией о констрейнтах
* **batchSize** - размер батча генерации
* **generationPoolThreadSize** - размер пула потоков для генерации
* **globStoreThreads** - количество потоков сохранения для всех таблиц
* **tableStoreThreads** - количество потоков сохранения для одной таблицы

Например:
```bash
java -jar ./build/libs/datagen-1.0-SNAPSHOT.jar -config <datagen_config.yaml>
```

Где `datagen_config.yaml` - это:

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



