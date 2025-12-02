# Установка

1. Перенести папку pg_ozerki в папку <место_установки_postgres>/postgresql/contrib/
2. Из папки <место_установки_postgres>/postgresql/contrib/pg_ozerki запустить:
    ```bash
    make install
    ```


# Использование

## Расширение
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


## Приложение

***ВАЖНО:*** схема импортируется в только что созданную БД, т.е. нужно перед этим сделать

```sql
DROP <имя_тестовой_БД>;
CREATE <имя_тестовой_БД>;
```

**Сборка:**
```bash
./gradlew build
```

**Пример запуска:**

```bash
java -jar build/libs/datagen-1.0-SNAPSHOT.jar -host localhost -port 5432 -dbname new -user shadowplay -passwd postgres -schema scripts/srcdb_schema.sql -stat scripts/stats.csv
```

Где:

**-host** - хост тестовой БД

**-port** - порт тествовой БД

**-dbname** - имя тестовой БД

**-user** - имя пользователя

**-passwd** - пароль

**-schema** - путь к файлу экспорта схемы, полученному из исходной БД с помощью расширения

**-stat** - путь к файлу экспорта статистики, полученному из исходной БД с помощью расширения
