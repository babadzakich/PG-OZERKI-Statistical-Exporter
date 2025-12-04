# Установка

1. Перенести папку pg_ozerki в папку <место_установки_postgres>/postgresql/contrib/
2. Из папки <место_установки_postgres>/postgresql/contrib/pg_ozerki запустить:
    ```bash
    make install
    ```


# Использование
`dump_statistic()` - экспорт статистики в csv файл, который потом передается в приложение

`dump_schema()` - экспорт структуры

Экспорт в .sql скрипт:


***Схема:***

```bash
./psql -XAtq -c "SELECT dump_schema();" <имя_БД> > schema.sql
 ```

***ВАЖНО:*** схема импортируется в только что созданную БД, т.е. нужно перед этим сделать

```sql
DROP <имя_БД>;
CREATE <имя_БД>;
```


 ***Статистика:***

```bash
 ./psql -P 'null=NULL' --csv -c "SELECT * from dump_statistic();" <имя_БД> > stats.csv
``` 