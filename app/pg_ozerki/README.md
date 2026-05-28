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