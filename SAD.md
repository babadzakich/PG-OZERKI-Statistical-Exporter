# System Architecture Document (SAD)
## PG-OZERKI Statistical Exporter

---

## 1. Executive Summary

**PG-OZERKI Statistical Exporter** — решение для генерации тестовых данных PostgreSQL на основе статистики исходной БД. Состоит из двух независимо собираемых компонентов:

- **`pg_ozerki`** — C-утилита, использующая клиентскую библиотеку PostgreSQL (libpq) для извлечения схемы, констрейнтов, индексов, плана запроса и колоночной статистики из живой БД.
- **`datagen`** — Java-приложение, по этим артефактам восстанавливающее целевую БД и наполняющее её синтетическими данными, статистически близкими к исходным (по MCV, гистограммам, NULL-долям, ndistinct и FK-связям).

**Цель:** получить тестовую БД, на которой EXPLAIN-план целевого запроса структурно совпадает с планом на исходной БД, **без передачи реальных данных** (важно для регуляторно-чувствительных доменов).

---

## 2. System Overview

### 2.1 Основные возможности

- Экспорт DDL (`CREATE TABLE`, индексы, констрейнты PK/UNIQUE/FK/CHECK).
- Экспорт колоночной статистики из `pg_stats` / `pg_class`: MCV, частоты MCV, гистограммные границы, `n_distinct`, `null_frac`, `row_count`.
- Экспорт `EXPLAIN (FORMAT JSON)` для опорного запроса.
- Генерация данных по статистике: NULL-доля → MCV-значения с их частотами → значения из гистограммных бакетов.
- Поддержка single- и composite PK/UNIQUE и FK (1:1, 1:N).
- Высокопроизводительная загрузка через `COPY FROM STDIN`.
- Многопоточная генерация и загрузка по уровням FK-зависимостей.

### 2.2 Целевые пользователи

- DBA / специалисты по оптимизации запросов: воспроизведение прод-планов на тест-стенде.
- Команды нагрузочного тестирования: реалистичные объёмы без копирования прод-данных.
- Регуляторно-чувствительные сценарии (банки, медицина, телеком): тест-данные без PII.

---

## 3. Architecture Overview

### 3.1 Общая архитектура

```
┌────────────────────────────┐                  ┌────────────────────────────┐
│   Source PostgreSQL DB   │                  │   Target PostgreSQL DB  │
│   (production / sample)  │                  │   (test environment)    │
└──────────────┬─────────────┘                   └──────────────▲────────────┘
               │                                            │
               │ libpq                                      │ JDBC + COPY
               │                                            │
        ┌──────▼──────┐                                ┌──────┴──────┐
        │  pg_ozerki  │                               │   datagen  │
        │   (C, CLI)  │                               │ (Java, CLI)│
        └──────┬──────┘                                └──────▲──────┘
               │                                            │
               │  writes                               reads│
               ▼                                            │
        ┌─────────────────────────────────────────────────────────┴┐
        │                  Intermediate Artifacts            │
        │  schema.sql   stats.csv   constraints.csv          │
        │  indexes.sql  explain.json   query.sql             │
        └──────────────────────────────────────────────────────────┘
```

Артефакты — единственный канал коммуникации между компонентами. Это сознательное решение: pg_ozerki и datagen можно запускать на разных хостах и в разное время.

---

## 4. Component Architecture

### 4.1 pg_ozerki (экспортёр)

Назначение: вытащить из работающей БД всё, что нужно для последующей генерации, в текстовые файлы.

```
pg_ozerki <config.yaml>
  │
  ├─ YAML loader (libyaml)
  ├─ libpq connection
  │
  ├─ Schema dumper      → schema.sql      (CREATE TABLE / SEQUENCE / VIEW)
  ├─ Index dumper       → indexes.sql     (CREATE INDEX, не-PK/UNIQUE)
  ├─ Constraints dumper → constraints.csv (PK / UNIQUE / FK; CHECK — опционально)
  ├─ Statistics dumper  → stats.csv       (по строке на колонку, 16 полей)
  └─ EXPLAIN dumper     → explain.json    (план опорного запроса)
```

**Источники данных в PostgreSQL:** `pg_class`, `pg_attribute`, `pg_constraint`, `pg_index`, `pg_stats`, `pg_statistic`, `information_schema.*`.

**Флаги конфигурации:** `no-checks` (не экспортировать CHECK), `no-exts` (пропустить EXTENSION).

### 4.2 datagen (генератор)

Java-приложение, fat JAR. Главный класс — `ru.nsu.datagen.Main`. Структура пакетов:

```
ru.nsu.datagen
├── Main                              — CLI entry point (JCommander)
├── argValidation                     — валидация CLI / YAML аргументов
├── pipeline
│   └── Pipeline                      — HikariCP pool + thread pools + orchestration
├── importer
│   └── Importer                      — выполняет schema.sql и парсит CSV-метаданные
├── dataGenerator
│   ├── DatabaseDataGenerator         — обход зависимостей, генерация по уровням
│   ├── DataGenerator                 — генерация одной таблицы
│   ├── graph
│   │   ├── DependencyGraph           — JGraphT-граф FK-связей
│   │   └── TableDependency
│   ├── model                         — TableMetadata, ColumnMetadata, *CSV DTOs
│   ├── generators
│   │   ├── ColumnGenerator           — интерфейс
│   │   ├── unique/                   — SimpleUniqueGenerator, MarkovGenerator
│   │   ├── fk/                       — OneToOneFK, OneToManyFK (single + composite)
│   │   ├── normal/                   — StatTypeBasedGenerator (NULL + MCV + hist)
│   │   └── numbergenerator/          — ValueGenerator по типам данных
│   │       ├── numeric/ datetime/ binary/ geoma/ range/
│   │       ├── StringValueGenerator, BooleanValueGenerator
│   │       ├── JSONValueGenerator,   MoneyValueGenerator
│   │       └── ValueGeneratorFactory — выбор реализации по data_type
│   └── store
│       └── TableStore                — COPY FROM STDIN, виртуальные потоки
```

---

## 5. Pipeline Architecture

### 5.1 Export pipeline (pg_ozerki)

```
config.yaml ──► libpq connect ──► pg_class / pg_stats / pg_constraint / pg_index
                                    │
                                    ├─► serialize DDL          ─► schema.sql
                                    ├─► serialize indexes      ─► indexes.sql
                                    ├─► serialize constraints  ─► constraints.csv
                                    ├─► EXPLAIN (FORMAT JSON)  ─► explain.json
                                    └─► per-column stats row   ─► stats.csv
```

### 5.2 Generation pipeline (datagen)

```
Phase 0: Bootstrap
─────────────────────────────────────────────────────────
  Main.main → JCommander → CLI args + optional -config YAML
            → Pipeline.run(args)
                ├─ HikariCP pool (size: connectionPoolSize, default 10)
                │  JDBC URL: jdbc:postgresql://host:port/dbname
                │            ?currentSchema=schema
                │            &reWriteBatchedInserts=true
                ├─ Generation thread pool   (generationThreadPoolSize, default 4)
                └─ Store thread pools       (globStoreThreads × tableStoreThreads)


Phase 1: Schema import
─────────────────────────────────────────────────────────
  Importer
    ├─ читает schema.sql и выполняет его на целевой БД (создаёт пустые таблицы)
    ├─ парсит stats.csv через OpenCSV  → List<ColumnMetadataCSV>
    ├─ парсит constraints.csv          → List<ConstraintCSV>
    └─ TableMetadataMaker.build(...)   → Map<String, TableMetadata>


Phase 2: Dependency graph
─────────────────────────────────────────────────────────
  DependencyGraph (JGraphT DefaultDirectedGraph)
    ├─ узлы — полные имена таблиц "schema.table"
    ├─ рёбра — FK (child → parent)
    ├─ ConnectivityInspector выделяет weakly connected components
    └─ внутри каждого компонента — обход по уровням (BFS по топологии)


Phase 3: Per-level generation (DatabaseDataGenerator)
─────────────────────────────────────────────────────────
  for each component:
    for each level (топологический уровень):
        CompletableFuture.allOf(
            tables_on_this_level.map(t -> generate(t))
        ).join();

  Таблицы одного уровня генерируются параллельно;
  следующий уровень стартует только когда предыдущий полностью загружен в БД
  (FK-родители должны быть в БД до загрузки FK-детей).


Phase 4: Per-table generation (DataGenerator)
─────────────────────────────────────────────────────────
  Для каждой колонки выбирается ОДНА из трёх стратегий:

  ┌──────────────────────────────────────────────────────────────────┐
  │ Strategy A — Unique                                              │
  │   условие: isPrimaryKey || isUnique || ndistinct == -1           │
  │   single-column   → SimpleUniqueGenerator                        │
  │   composite       → MarkovGenerator (взвешенный по MCV сэмплинг) │
  │                                                                  │
  │ Strategy B — Foreign Key                                         │
  │   условие: isForeignKey                                          │
  │   ForeignKeyGeneratorFactory выбирает по RelationshipType:       │
  │     ONE_TO_ONE   → OneToOneForeignKeyGenerator (single+composite)│
  │     ONE_TO_MANY  → OneToManyForeignKeyGenerator (single+composite)│
  │   значения сэмплируются из generatedData родительских таблиц     │
  │                                                                  │
  │ Strategy C — Normal                                              │
  │   StatTypeBasedGenerator:                                        │
  │     1) null_percent * row_count позиций → NULL                   │
  │     2) MCV-значения по их частотам                               │
  │     3) остаток — из гистограммных бакетов через ValueGenerator   │
  └──────────────────────────────────────────────────────────────────┘

  Батч-цикл (batchSize, default 10000):
    ColumnBatchState  ─► generate(batchSize)  ─► List<List<Object>>
    TableStore.storeTable(...)                ─► COPY FROM STDIN
    при ошибке COPY: rollbackToPreviousState() на ColumnBatchState


Phase 5: Storage (TableStore)
─────────────────────────────────────────────────────────
  Используется PostgreSQL COPY FROM STDIN через org.postgresql.copy.CopyManager.

  storeTable(table, columnData, parallelism):
    ├─ split в чанки по 50 000 строк
    ├─ для каждого чанка — Thread.ofVirtual().start():
    │     connection.unwrap(BaseConnection.class)
    │     SET search_path TO public, bookings
    │     COPY <table> (<cols>) FROM STDIN
    │       WITH (FORMAT CSV, HEADER FALSE, NULL 'NULL_MARKER')
    │     CopyManager.copyIn(sql, Reader)
    └─ join all virtual threads


Phase 6: FK propagation
─────────────────────────────────────────────────────────
  Сгенерированные значения родительских колонок (на которые ссылаются FK)
  складываются в shared Map<String, List<Object>> generatedData
  с ключом "schema.table.column".
  FK-генераторы детей читают из этой Map.
  По завершении генерации всех ссылающихся таблиц запись эвиктится
  (контроль памяти).


Phase 7: Finalization
─────────────────────────────────────────────────────────
  Pipeline закрывает HikariCP pool и executor'ы.
  В логах: total rows / table, errors, время фаз.
```

---

## 6. Data Flow

### 6.1 Полный поток

```
Source PostgreSQL
   │
   │ libpq (pg_ozerki)
   ▼
┌──────────────────────────────────────────────────────────────────────┐
│ Filesystem artifacts                                          │
│  schema.sql       CREATE TABLE / SEQUENCE / VIEW              │
│  indexes.sql      CREATE INDEX (не-PK/UNIQUE)                 │
│  constraints.csv  constraint_name,type,columns                │
│  stats.csv        16 колонок (см. §6.2)                       │
│  explain.json     EXPLAIN (FORMAT JSON) опорного запроса      │
│  query.sql        текст опорного запроса                      │
└──────────────────────────────────────────────────────────────────────┘
   │
   │ JDBC + COPY (datagen)
   ▼
Target PostgreSQL  (recreated schema + synthetic rows)
   │
   │ EXPLAIN (FORMAT JSON) [verification step]
   ▼
План на target ≈ план на source  (tree edit distance > 50% — критерий IntegrationTest)
```

### 6.2 Формат stats.csv

Одна строка на колонку. 16 полей:

| Поле | Описание |
|---|---|
| `table_schema` | имя схемы |
| `table_name` | имя таблицы |
| `column_name` | имя колонки |
| `data_type` | PostgreSQL data type |
| `modifiers` | space-separated флаги: `PK`, `FK`, `UNIQUE` |
| `null_percent` | доля NULL (0.0 – 1.0) |
| `row_count` | оценка числа строк в таблице |
| `max_length` | макс. длина значения (для varchar) |
| `relation_types` | тип отношения (`r`/`v`/...) |
| `incoming_references` | список ссылок ИЗ других колонок на эту, через `, ` |
| `outcoming_references` | `schema.table.column` цели FK (через `,` для composite) |
| `mcv` | массив most-common values в PostgreSQL-формате `{v1,v2,...}` |
| `mcv_frequencies` | массив частот MCV |
| `avg_column_width_bytes` | средняя ширина значения в байтах |
| `ndistinct` | число различных значений; `-1` = все уникальны; отрицательная дробь конвертируется в абсолют |
| `hbounds` | массив histogram bounds в PostgreSQL-формате |

### 6.3 Формат constraints.csv

`constraint_name, type, columns`. `type ∈ {PK, UNIQUE, FK}`. `columns` — comma-separated `schema.table.column`. CHECK-констрейнты в этой версии не используются генератором (даже если экспортированы).

---

## 7. Technologies & Dependencies

### 7.1 pg_ozerki

| Компонент | Назначение |
|---|---|
| PostgreSQL ≥ 15 | целевая СУБД + исходники для сборки |
| C99 | язык реализации |
| libpq | клиентская библиотека PostgreSQL |
| libyaml | парсинг YAML-конфига |
| Make + `pg_config` | сборка как утилиты в `src/bin/` |

### 7.2 datagen

```
Сборка
  Gradle 8.x (wrapper)             ./gradlew jar
  io.freefair.lombok 9.2.0         Lombok plugin
  JDK 21+                          язык + виртуальные потоки (TableStore)

Runtime
  PostgreSQL JDBC      42.2.21     драйвер + CopyManager / BaseConnection
  HikariCP             5.1.0       connection pool
  JGraphT              1.5.2       FK dependency graph, ConnectivityInspector
  OpenCSV              5.12.0      парсинг stats.csv / constraints.csv
  JCommander           2.0         CLI args
  SnakeYAML            2.0         YAML-конфиг
  Jackson (BOM 2.18.2) databind/core/annotations — JSON (explain.json, тесты)
  DataFaker            2.4.2       вспомогательный source случайных строк
  Lombok               1.18.30     boilerplate
  SLF4J 2.0.12 + Logback 1.5.13    логирование

Тесты
  JUnit 5 (BOM 5.10.0)
  AssertJ              3.24.2
  Testcontainers       1.20.4 (postgresql) — поднимает postgres:17 в Docker
  maxHeapSize          6 GB

Артефакт
  Uber-JAR  build/libs/datagen-1.0-SNAPSHOT.jar
  Main-Class: ru.nsu.datagen.Main
```

---

## 8. Quality Attributes

### 8.1 Performance

- **Storage path:** `COPY FROM STDIN` через `CopyManager`, не JDBC batch — основной источник throughput.
- **Параллелизм:** виртуальные потоки в `TableStore` (1 chunk = 1 virtual thread); таблицы одного уровня FK-графа генерируются параллельно через `CompletableFuture`.
- **Размеры по умолчанию:** `batchSize=10000`, chunk=50000, generation pool=4, connection pool=10.
- **Память:** `generatedData` Map хранит сгенерированные FK-target значения до того, как все потребители их прочтут; эвикция — по завершении всех зависимых таблиц.

### 8.2 Scalability

- Узкое место — IO на стороне целевого PostgreSQL (WAL, индексы, FK-проверки).
- Тонкая настройка через CLI/YAML: `batchSize`, `generationThreadPoolSize`, `connectionPoolSize`, `globStoreThreads`, `tableStoreThreads`.
- Графовый подход поддерживает многокомпонентные базы без полной перестройки топ-сорта.

### 8.3 Reliability

- При неуспехе `COPY` для батча — `ColumnBatchState.rollback()` сбрасывает состояние генератора этой колонки, чтобы повторный батч не нарушил уникальность.
- Иные SQL-ошибки логируются; работа над политикой подсчёта реально сохранённых строк ведётся (см. ветка `line-creation-impl`, переработка `MarkovGenerator`).
- Целостность FK обеспечивается порядком обхода графа: родитель полностью загружен до начала генерации детей.

### 8.4 Maintainability

- Стратегия выбора генератора чётко локализована в `DataGenerator` (три ветки: unique / fk / normal).
- Каждый тип данных PostgreSQL имеет отдельный класс `ValueGenerator` в `numbergenerator/*` — расширение через добавление класса + регистрацию в `ValueGeneratorFactory`.
- Lombok убирает boilerplate, но требует наличия плагина в Gradle и в IDE.

### 8.5 Known limitations / quirks

- CHECK-констрейнты не учитываются генератором.
- Расширенная статистика PostgreSQL (`pg_statistic_ext`) не используется.
- `EXPLAIN ANALYZE` (фактические тайминги) экспортируется, но генератором не используется.
- Не учитываются сложные кейсы пересекающихся констреинтов

---

## 9. Configuration & Deployment

### 9.1 Конфиг экспортёра (`export_config.yaml`)

```yaml
dbname: src
username: postgres
host: localhost
port: 5432
schema-file: src_schema.sql
stats-file:  src_stats.csv
constr-file: src_constr.csv
index-file:  src_indexes.sql
query-file:  src_query.sql
explainfile: src_explain.json
# optional:
# explainfile-analyze: src_explain_analyze.json
# query: "SELECT ..."     # альтернатива query-file
no-checks:                # пропустить CHECK
no-exts:                  # пропустить EXTENSION
```

### 9.2 Конфиг генератора (`datagen_config.yaml`)

```yaml
host: localhost
port: 5432
user: postgres
password: postgres
dbname: new

schemaPath:     src_schema.sql
statPath:       src_stats.csv
indexFile:      src_indexes.sql
constraintFile: src_constr.csv

batchSize:                50000
generationThreadPoolSize: 4
connectionPoolSize:       10
globStoreThreads:         1
tableStoreThreads:        1
```

CLI-флаги имеют приоритет над YAML.

### 9.3 Запуск (golden path)

```bash
# 1. экспорт
./pg_ozerki export_config.yaml

# 2. генерация
java -jar datagen-1.0-SNAPSHOT.jar -config datagen_config.yaml

# 3. верификация
psql -d new -c "EXPLAIN (FORMAT JSON) <opcrnal_query>"
# план сравнивается с src_explain.json (tree edit distance > 50%)
```

Воспроизводимый end-to-end сценарий — в `tutorial/`.

---

## 10. Glossary

| Термин | Определение |
|---|---|
| **pg_ozerki** | C-утилита экспорта схемы и статистики из PostgreSQL |
| **datagen** | Java-приложение генерации синтетических данных |
| **MCV** | Most Common Values — наиболее частые значения колонки и их частоты (из `pg_stats`) |
| **Histogram bounds** | Границы гистограммных бакетов из `pg_stats.histogram_bounds` |
| **ndistinct** | Число различных значений колонки; `-1` = все уникальны |
| **COPY FROM STDIN** | PostgreSQL-протокол bulk-загрузки; используется через `CopyManager` |
| **Weakly connected component** | Компонент связности в неориентированной проекции FK-графа |
| **ColumnBatchState** | Обёртка над `ColumnGenerator` + связанными колонками; поддерживает rollback батча |
| **generatedData** | Shared map `schema.table.column → List<Object>` для передачи FK-значений детям |
| **NULL_MARKER** | Литерал в CSV-потоке COPY, интерпретируемый PostgreSQL как `NULL` |
| **Tree edit distance** | Метрика сходства EXPLAIN-планов; в `IntegrationTest` требуется > 50% |

---

**Document Version:** 1.0
**Last Updated:** 2026-05-27