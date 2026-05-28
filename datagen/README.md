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