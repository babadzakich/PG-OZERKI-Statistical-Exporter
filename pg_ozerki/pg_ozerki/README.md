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

```bash
 ./psql <имя_БД> -t -X -A -c "SELECT dump_schema();" > test.sql
 ```