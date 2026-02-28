# pg_ozerki

Утилита для эскпорта в проекте **PG OZERKI STATISTICAL EXPORTER**


# Сборка

1. Перенести папку **pg_ozerki**  в **src/bin/**
2. Из неё запустить 
    ```bash
    make install
    ```

# Использование

Запуск:
```bash
./pg_ozerki <флаги>
```

Где флаги:

* **--dbname** - имя БД
* **--username** - имя пользователя
* **--host** - имя хоста
* **--port** - порт
* **--schema-file** - имя файла, в который экспортируется схема (.sql)
* **--stats-file** - имя файла, в который экспортируется статистика (.csv)
* **--query** - текст запроса в кавычках, по которому будет выполняться экспорт
* **--explainfile** - имя файла, куда будет экспортироваться результат EXPLAIN в формате json для запроса

* **--explainfile-analyze** - имя файла, куда будет экспортироваться результат EXPLAIN ANALYZE в формате json для запроса

Например:
```bash
./pg_ozerki --dbname demo --username shadowplay --host localhost --port 5432 --schema-file 11.sql --stats-file 11.csv --query "select * from bookings.tickets" --explainfile exp.json
```
