# Установка

1. Перенести содержимое в папку contrib
2. Из папки расширения:
    ```bash
    make install
    ```


# Использование
`dump_statistic()` - экспорт статистики, идентично расщирению dump_stat

`dump_schema()` - экспорт структуры (пока только таблицы)

Экспорт в .sql скрипт:


***Схема***:

```bash
./psql -XAq -c "SELECT dump_schema();" <имя_БД> > schema.sql
 ```

 ***Статистика***

```bash
 ./psql -P 'null=NULL' --csv -c "SELECT * from dump_statistic();" <имя_БД> > stats.csv
``` 