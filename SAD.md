# System Architecture Document (SAD)
## PG-OZERKI Statistical Exporter

---

## 1. Executive Summary

**PG-OZERKI Statistical Exporter** - это комплексное решение для создания тестовых данных на основе статистики PostgreSQL. Система состоит из двух основных компонентов: расширения PostgreSQL для экспорта схемы и статистики, и Java-приложения для генерации синтетических данных.

**Цель:** Автоматизировать создание тестовых наборов данных, которые статистически соответствуют реальным данным, без необходимости экспорта и импорта больших объемов данных.

---

## 2. System Overview

### 2.1 Основные возможности

- **Экспорт схемы БД** - полная структура таблиц, индексов, констрейнтов
- **Сбор статистики** - информация о распределении данных в таблицах
- **Генерация данных** - создание синтетических данных на основе статистики
- **Поддержка сложных отношений** - иностранные ключи, зависимости между таблицами
- **Параллельная обработка** - многопоточная генерация и сохранение данных

### 2.2 Целевые пользователи

- Специалисты по тестированию производительности БД
- DevOps инженеры
- Специалисты по миграции БД
- Разработчики, нуждающиеся в тестовых данных

---

## 3. Architecture Overview

### 3.1 Общая архитектура системы

```
┌─────────────────────────────────────────────────────────────┐
│                    PG-OZERKI System                          │
└─────────────────────────────────────────────────────────────┘
                              │
                ┌─────────────┴─────────────┐
                │                           │
        ┌───────▼────────┐         ┌───────▼────────┐
        │  pg_ozerki     │         │  DataGen       │
        │  Extension     │         │  Java App      │
        │  (PostgreSQL)  │         │  (Gradle)      │
        └───────┬────────┘         └───────┬────────┘
                │                          │
        ┌───────▼───────┐          ┌──────▼──────┐
        │ Export Phase  │          │ Gen Phase   │
        │ - Schema      │          │ - Generate  │
        │ - Statistics  │          │ - Store     │
        └───────┬───────┘          └──────┬──────┘
                │                         │
        ┌───────▼──────────────────────────▼──────┐
        │      PostgreSQL Database               │
        │  ┌──────────────┐  ┌────────────────┐  │
        │  │   Schema     │  │  Test Data     │  │
        │  │ (DDL+Index)  │  │  (Synthetic)   │  │
        │  └──────────────┘  └────────────────┘  │
        └────────────────────────────────────────┘

Output Files:
├── schema.sql          → Database structure
├── stats.csv           → Column statistics
├── indexes.sql         → Non-strict indexes
└── constraints.csv     → Table constraints
```

---

## 4. Component Architecture

### 4.1 pg_ozerki Extension (PostgreSQL C Extension)

**Назначение:** Экспорт метаинформации из работающей БД

```
PostgreSQL Server
│
├─ pg_ozerki Extension
│  │
│  ├─ dump_schema()
│  │  ├─ Read system catalog (pg_tables, pg_attributes)
│  │  ├─ Extract DDL statements
│  │  ├─ Include indexes, constraints, triggers
│  │  └─ Output: SQL statements
│  │
│  ├─ dump_statistic()
│  │  ├─ Access pg_stat_user_tables
│  │  ├─ Access pg_stat_user_columns
│  │  ├─ Extract row counts, null values
│  │  ├─ Extract min/max values
│  │  └─ Output: CSV format
│  │
│  └─ Utility Functions
│     ├─ get_column_stats()
│     ├─ format_constraint()
│     └─ serialize_to_csv()
│
└─ System Catalogs
   ├─ pg_tables
   ├─ pg_attributes
   ├─ pg_constraints
   ├─ pg_indexes
   └─ pg_stat_user_*
```

**Ключевые функции:**

1. `dump_schema()` - Экспортирует полную схему БД
   - Читает системные каталоги
   - Генерирует CREATE TABLE, CREATE INDEX команды
   - Включает CHECK и FOREIGN KEY ограничения

2. `dump_statistic()` - Экспортирует статистику
   - Количество строк в таблице
   - Null-значения в столбцах
   - Min/Max значения
   - Формат: CSV

---

### 4.2 DataGen Java Application

**Назначение:** Генерация синтетических данных на основе экспортированной информации

```
DataGen Application (Java)
│
├─ Initialization Layer
│  ├─ Main class
│  ├─ Arguments Parser (JCommander)
│  └─ Configuration Loader (YAML)
│
├─ Schema Processing
│  ├─ SQL Parser
│  │  ├─ Parse CREATE TABLE statements
│  │  ├─ Extract column definitions
│  │  ├─ Build table metadata
│  │  └─ Create dependency graph
│  │
│  └─ Metadata Extractor
│     ├─ Table structure
│     ├─ Column types
│     ├─ Primary/Foreign keys
│     └─ Constraints
│
├─ Statistics Processing
│  ├─ CSV Reader (OpenCSV)
│  ├─ Statistics Parser
│  │  ├─ Parse column statistics
│  │  ├─ Extract distribution info
│  │  ├─ Calculate null percentages
│  │  └─ Store in memory
│  │
│  └─ Constraint Processor
│     ├─ Parse CHECK constraints
│     ├─ Parse FOREIGN KEY relations
│     └─ Build constraint models
│
├─ Data Generation Engine
│  ├─ Generator Coordinator
│  │  ├─ Resolve table dependencies
│  │  ├─ Execute topological sort
│  │  └─ Schedule generation tasks
│  │
│  ├─ Data Generators (DataFaker)
│  │  ├─ Numeric Generator
│  │  ├─ String Generator
│  │  ├─ Date/Time Generator
│  │  ├─ UUID/JSON Generator
│  │  └─ Custom Generators
│  │
│  ├─ Constraint Applier
│  │  ├─ Apply CHECK constraints
│  │  ├─ Ensure FK references
│  │  └─ Validate unique constraints
│  │
│  └─ Batch Manager
│     ├─ Collect records (batch size)
│     ├─ Validate batch
│     └─ Queue for storage
│
├─ Storage Layer
│  ├─ Connection Pool (HikariCP)
│  │  ├─ Manage DB connections
│  │  ├─ Thread-safe operations
│  │  └─ Connection pooling
│  │
│  ├─ Batch Inserter
│  │  ├─ Prepare batch INSERT statements
│  │  ├─ Execute bulk inserts
│  │  └─ Commit transactions
│  │
│  ├─ Store Coordinator
│  │  ├─ Manage global store threads
│  │  ├─ Manage table store threads
│  │  └─ Load balancing
│  │
│  └─ Transaction Manager
│     ├─ Handle transactions
│     ├─ Retry on failure
│     └─ Logging & monitoring
│
└─ Utilities
   ├─ Logger (Logback/SLF4J)
   ├─ Exception Handler
   └─ Performance Metrics
```

---

## 5. Pipeline Architecture

### 5.1 Export Pipeline (pg_ozerki)

```
EXPORT PIPELINE
═══════════════════════════════════════════════════════════════

Step 1: Configuration Loading
────────────────────────────────
Input: config.yaml
  ├─ dbname
  ├─ username
  ├─ host
  ├─ port
  └─ schema-file, stats-file, etc.
Output: PostgreSQL Connection

Step 2: Database Connection
────────────────────────────────
PostgreSQL CLI (psql) with:
  ├─ -XAtq flags (non-interactive mode)
  ├─ Connection parameters
  └─ Query execution

Step 3: Schema Export (dump_schema)
────────────────────────────────────
Query: SELECT dump_schema();
Process:
  1. Read pg_tables for table list
  2. Read pg_attributes for column definitions
  3. Read pg_indexes for index definitions
  4. Read pg_constraints for constraints
  5. Generate CREATE TABLE statements
  6. Generate CREATE INDEX statements
  7. Include CHECK constraints
Output: schema.sql

Step 4: Statistics Export (dump_statistic)
────────────────────────────────────────────
Query: SELECT * from dump_statistic();
Process:
  1. Access pg_stat_user_tables (row counts)
  2. Access pg_stat_user_columns (null counts)
  3. Extract min/max values per column
  4. Format as CSV
  5. Include distribution hints
Output: stats.csv

Step 5: Constraints Export
──────────────────────────────
Query: Generate constraint queries
Process:
  1. Extract FOREIGN KEY definitions
  2. Extract CHECK constraints
  3. Extract UNIQUE constraints
  4. Serialize to CSV format
Output: constraints.csv

Step 6: Index Export (optional)
────────────────────────────────
Query: Generate non-strict indexes
Process:
  1. Read pg_indexes
  2. Filter strict indexes (PK, FK)
  3. Generate CREATE INDEX statements
Output: indexes.sql

Step 7: EXPLAIN Export (optional)
────────────────────────────────────
Query: Custom query analysis
Process:
  1. Execute EXPLAIN on provided query
  2. Execute EXPLAIN ANALYZE on query
  3. Capture query plans as JSON
Output: explain.json, explain-analyze.json


EXPORT FLOW DIAGRAM
════════════════════════════════════════════════════════════════

config.yaml
    ▼
┌──────────────────────────────┐
│  PostgreSQL Connection       │
└──────────────────────────────┘
    │
    ├──────────────────────┬──────────────────────┬──────────────┐
    ▼                      ▼                      ▼              ▼
┌─────────────┐   ┌──────────────┐   ┌──────────────┐   ┌─────────────┐
│dump_schema()│   │dump_statistic│   │Constraints   │   │EXPLAIN      │
└─────────────┘   └──────────────┘   │Export        │   │Export       │
    ▼                      ▼          └──────────────┘   └─────────────┘
    ▼                      ▼                  ▼                  ▼
schema.sql              stats.csv      constraints.csv    explain.json


EXECUTION COMMANDS
════════════════════════════════════════════════════════════════

# Export Schema
$ psql -XAtq -c "SELECT dump_schema();" TARGET_DB > schema.sql

# Export Statistics  
$ psql -P 'null=NULL' --csv -c \
  "SELECT * from dump_statistic();" TARGET_DB > stats.csv

# Export Constraints
$ psql -P 'null=NULL' --csv -c \
  "SELECT * from dump_constraints();" TARGET_DB > constraints.csv
```

---

### 5.2 Generation Pipeline (DataGen)

```
GENERATION PIPELINE
═══════════════════════════════════════════════════════════════

Phase 0: Initialization
────────────────────────────────
1. Parse command-line arguments (JCommander)
   └─ -config config.yaml

2. Load YAML configuration
   ├─ Database connection params
   ├─ File paths
   ├─ Thread pool settings
   └─ Batch size settings

3. Initialize logging (Logback/SLF4J)

4. Create connection pool (HikariCP)
   └─ Max connections: 10-20
   └─ Connection timeout: 30s


Phase 1: Schema Processing
────────────────────────────────
1. Read schema.sql file

2. Parse SQL statements
   ├─ Identify CREATE TABLE
   ├─ Extract column definitions
   ├─ Extract data types
   ├─ Extract constraints
   └─ Build table metadata

3. Build Metadata Model
   ├─ TableMetadata
   │  ├─ tableName
   │  ├─ columns: List<ColumnMetadata>
   │  ├─ primaryKeys: List<String>
   │  ├─ foreignKeys: List<ForeignKeyConstraint>
   │  └─ checkConstraints: List<CheckConstraint>
   │
   └─ ColumnMetadata
      ├─ columnName
      ├─ dataType (INT, VARCHAR, DATE, etc.)
      ├─ isNullable
      ├─ isPrimaryKey
      └─ constraints: List<String>

4. Build Dependency Graph (JGraphT)
   ├─ Node: Each table
   ├─ Edge: Foreign key relationship
   ├─ Direction: From FK table to referenced table
   └─ Purpose: Topological sort for generation order


Phase 2: Statistics Processing
────────────────────────────────
1. Read stats.csv file
   └─ Format: table_name, column_name, stat_type, value

2. Parse each row
   ├─ Extract table name
   ├─ Extract column name
   ├─ Extract statistic type
   │  ├─ row_count
   │  ├─ null_ratio
   │  ├─ min_value
   │  ├─ max_value
   │  └─ distinct_count
   └─ Extract value

3. Build Statistics Model
   ├─ TableStatistics
   │  ├─ tableName
   │  ├─ rowCount: int
   │  └─ columnStats: Map<String, ColumnStatistics>
   │
   └─ ColumnStatistics
      ├─ columnName
      ├─ nullRatio: double (0.0-1.0)
      ├─ minValue: String
      ├─ maxValue: String
      ├─ distinctCount: int
      ├─ valueDistribution: Map
      └─ constraints: List<String>

4. Load Constraints (if constraints.csv exists)
   ├─ Parse each constraint
   ├─ Build constraint models
   └─ Validate constraint compatibility


Phase 3: Generator Setup
────────────────────────────────
1. Resolve Generation Order
   ├─ Topological sort of dependency graph
   ├─ Order tables with no FK dependencies first
   ├─ Then tables with FK to previous tables
   └─ Fail if circular dependency detected

2. Initialize Data Generators (DataFaker)
   ├─ Load DataFaker provider
   ├─ Configure locale settings
   ├─ Setup seed for reproducibility
   └─ Configure custom providers

3. Create Generation Thread Pool
   ├─ Pool size: generationPoolThreadSize
   ├─ Queue type: LinkedBlockingQueue
   └─ Thread factory: Named threads

4. Create Storage Thread Pools
   ├─ Global store thread pool
   │  └─ Size: globStoreThreads
   │
   ├─ Per-table store thread pools
   │  └─ Size: tableStoreThreads per table
   │
   └─ All use executor service


Phase 4: Parallel Generation
────────────────────────────────
For each table in sorted order:

┌─ Table: users (no FK)
├─ Row count: 10,000
├─ Generation threads: 4
└─ Process:
   │
   ├─ Thread 1: Generate rows 0-2,500
   │  ├─ Read column stats
   │  ├─ Generate values using DataFaker
   │  │  ├─ For null_ratio: decide if NULL
   │  │  ├─ For VARCHAR: generate random string
   │  │  ├─ For INT: generate in min-max range
   │  │  ├─ For DATE: generate in date range
   │  │  └─ For custom: apply constraint logic
   │  ├─ Apply CHECK constraints
   │  ├─ Collect into batch (batch_size=50,000)
   │  └─ Queue batch to storage
   │
   ├─ Thread 2: Generate rows 2,500-5,000
   │  └─ Same process
   │
   ├─ Thread 3: Generate rows 5,000-7,500
   │  └─ Same process
   │
   └─ Thread 4: Generate rows 7,500-10,000
      └─ Same process

Parallel with generation:
│
├─ Storage Thread 1: Write batches
│  ├─ Take batch from queue
│  ├─ Prepare INSERT statement
│  │  └─ INSERT INTO users VALUES (?, ?, ...)
│  ├─ Execute batch (1000 rows per statement)
│  ├─ Commit transaction
│  └─ Log written row count
│
└─ Monitor threads
   ├─ Track progress
   ├─ Handle exceptions
   └─ Log statistics


Phase 5: Foreign Key Handling
────────────────────────────────
For tables with FK constraints:

Example: orders (contains FK to users)
│
├─ Prerequisite: users table already generated
│
├─ For each orders row to generate:
│  ├─ Generate order_id (PK)
│  ├─ Generate user_id (FK)
│  │  ├─ Valid range: 1 to max(users.id)
│  │  ├─ Random selection from generated user IDs
│  │  └─ Ensure referential integrity
│  └─ Generate other columns normally
│
└─ Result: All FK constraints satisfied


Phase 6: Batch Processing
────────────────────────────────
Batch Assembly:
1. Accumulate records until batch_size reached
2. Validate each record
   ├─ Check NOT NULL constraints
   ├─ Validate CHECK constraints
   ├─ Verify FK references
   └─ Validate unique constraints
3. Prepare batch
   ├─ Build INSERT statement
   ├─ Set parameter values
   └─ Queue for storage


Phase 7: Storage
────────────────────────────────
For each batch:
│
├─ Acquire database connection
│  └─ From HikariCP connection pool
│
├─ Start transaction
│  └─ BEGIN;
│
├─ Execute INSERT
│  ├─ Prepare INSERT statement
│  ├─ Bind parameters (values)
│  ├─ Execute batch
│  └─ Get inserted row count
│
├─ Handle conflicts
│  ├─ On unique constraint violation
│  ├─ On FK constraint violation
│  ├─ Retry or skip
│  └─ Log error
│
├─ Commit transaction
│  ├─ If successful: COMMIT;
│  ├─ If error: ROLLBACK;
│  └─ Connection returned to pool
│
└─ Progress update
   ├─ Increment total rows written
   ├─ Update progress bar
   └─ Log metrics


Phase 8: Finalization
────────────────────────────────
1. Wait for all generation threads to complete
2. Wait for all storage threads to complete
3. Close thread pools
4. Close database connections
5. Print final statistics
   ├─ Total rows generated per table
   ├─ Total execution time
   ├─ Rows per second
   └─ Any errors encountered


GENERATION FLOW DIAGRAM
════════════════════════════════════════════════════════════════

config.yaml ─┐
schema.sql ──┼─► Parse & Metadata
stats.csv ───┤   Building
constraints ─┘
    │
    ▼
┌──────────────────────────────┐
│ Build Dependency Graph       │
│ (Topological Sort)           │
└──────────────────────────────┘
    │
    ▼
┌──────────────────────────────┐
│ Determine Generation Order    │
│ (FK dependencies resolved)   │
└──────────────────────────────┘
    │
    ├─► Table 1 (no FK)
    ├─► Table 2 (no FK)
    ├─► Table 3 (FK→Table 1)
    └─► Table 4 (FK→Table 3)
    │
    ▼
For each table ────────┐
    │                  │
    ├─ Generation ◄────┤ (Parallel Threads)
    │  Threads         │
    │    │             │
    │    └─ Batch      ├─► Storage Threads ──► DB INSERT
    │       Assembly   │   (Parallel)
    │                  │
    └──────────────────┘
    │
    ▼
All tables complete
    │
    ▼
┌──────────────────────────────┐
│ Print Statistics             │
│ - Rows per table             │
│ - Total time                 │
│ - Throughput                 │
└──────────────────────────────┘
```

---

## 6. Data Flow Diagrams

### 6.1 Complete Data Flow

```
COMPLETE SYSTEM DATA FLOW
═══════════════════════════════════════════════════════════════

Source Database (Production)
│
├─ Schema Information
│  └─ SELECT dump_schema();
│     └─ schema.sql
│
├─ Statistics
│  └─ SELECT dump_statistic();
│     └─ stats.csv (columns: table, column, stat_type, value)
│
└─ Constraints Metadata
   └─ SELECT dump_constraints();
      └─ constraints.csv


Intermediate Storage (Local Filesystem)
│
├─ schema.sql          (DDL statements)
├─ stats.csv           (Statistical data)
├─ constraints.csv     (Constraint definitions)
└─ indexes.sql         (Index definitions)


DataGen Processing
│
├─ Parse Metadata
│  ├─ SQL Parser
│  ├─ Statistics Loader
│  └─ Constraint Validator
│
├─ Generate Synthetic Data
│  ├─ Table 1: 10,000 rows
│  ├─ Table 2: 50,000 rows
│  ├─ Table 3: 100,000 rows
│  └─ Table N: X rows
│
└─ Verify Constraints
   ├─ NOT NULL
   ├─ UNIQUE
   ├─ CHECK
   └─ FOREIGN KEY


Target Database (Test)
│
├─ Recreated Schema
│  └─ CREATE TABLE / CREATE INDEX
│
└─ Synthetic Test Data
   └─ INSERT INTO ... SELECT ... (bulk insert)


Output Artifacts
│
├─ Populated Test Database
├─ Generation Statistics
├─ Performance Metrics
└─ Validation Reports
```

---

## 7. Technologies & Dependencies

### 7.1 pg_ozerki Extension

| Component | Version | Purpose |
|-----------|---------|---------|
| PostgreSQL | 12+ | Core database system |
| C | C99 | Extension implementation |
| Make | 4.0+ | Build system |
| pg_config | System | PostgreSQL development headers |

### 7.2 DataGen Application

```
Build System
├─ Gradle          7.x+     (Build tool)
├─ Java            17+      (Runtime)
└─ Maven            (Dependency management via Gradle)

Core Dependencies
├─ PostgreSQL JDBC  42.2.21  (Database connectivity)
├─ JCommander       2.0      (Command-line arguments)
├─ OpenCSV          5.12.0   (CSV parsing)
├─ DataFaker        2.4.2    (Synthetic data generation)
└─ HikariCP         5.1.0    (Connection pooling)

Logging
├─ SLF4J            2.0.12   (Logging facade)
└─ Logback          1.5.13   (Logging implementation)

Serialization
├─ Jackson Core     2.18.2   (JSON processing)
├─ Jackson Databind 2.18.2
├─ Jackson Annotations 2.18.2
└─ SnakeYAML        2.0      (YAML configuration)

Graph Processing
├─ JGraphT Core     1.5.2    (Graph algorithms)
└─ JGraphT Ext      1.5.2    (Extended functionality)

Utilities
├─ Lombok           1.18.30  (Code generation)
└─ FreeCode Freefair 9.2.0  (Build plugins)

Testing
├─ JUnit 5          5.10.0   (Unit testing)
├─ AssertJ          3.24.2   (Fluent assertions)
├─ TestContainers   2.0.2    (Docker-based tests)
└─ PostgreSQL TC    1.20.4   (PostgreSQL containers)

JVM Settings
├─ Max Heap Size    6GB      (For tests)
├─ Java Version     17+      (Compilation target)
└─ Build Output     Uber JAR (all dependencies included)
```

---

## 8. Quality Attributes

### 8.1 Performance

- **Throughput:** 10,000-50,000 rows/second (depends on data complexity)
- **Memory:** ~4GB for 1M row generation
- **Parallel Processing:** 4-8 generation threads + storage threads
- **Batch Processing:** 50,000 rows per batch

### 8.2 Scalability

- Handles tables with millions of rows
- Parallel generation threads for large tables
- Connection pooling for efficient DB access
- Batch insertion to minimize transaction overhead

### 8.3 Reliability

- Transaction rollback on constraint violations
- Data validation before insertion
- Comprehensive error logging
- Automatic retry mechanism

### 8.4 Maintainability

- Modular architecture (separate generation/storage/validation)
- Configuration-driven (YAML)
- Comprehensive logging at all levels
- Clear separation of concerns

---

## 9. Configuration & Deployment

### 9.1 Export Configuration (config.yaml)

```yaml
# Database Connection
dbname: production_db
username: postgres
host: localhost
port: 5432

# Output Files
schema-file: exported_schema.sql
stats-file: exported_stats.csv
constr-file: exported_constraints.csv
index-file: exported_indexes.sql
explainfile: explain.json
explainfile-analyze: explain-analyze.json

# Query (optional)
query: "SELECT * FROM important_table"
query-file: query.sql

# Flags
no-checks:        # Skip CHECK constraints
no-exts:          # Skip extensions
```

### 9.2 Generation Configuration (config.yaml)

```yaml
# Database Connection
host: localhost
port: 5432
user: postgres
password: postgres
dbname: test_db

# Schema & Statistics
schemaPath: exported_schema.sql
statPath: exported_stats.csv
indexFile: exported_indexes.sql
constraintFile: exported_constraints.csv

# Performance Tuning
batchSize: 50000                  # Rows per batch
generationThreadPoolSize: 4       # Generation threads
globStoreThreads: 1               # Global store threads
tableStoreThreads: 1              # Per-table store threads
```

---

## 10. Future Extensibility

### 10.1 Planned Enhancements

1. **Advanced Statistics**
   - Column correlations
   - Temporal patterns
   - Data dependencies

2. **Custom Data Generators**
   - Domain-specific generators
   - Custom patterns
   - External data source integration

3. **Distributed Generation**
   - Multi-node data generation
   - Cloud deployment
   - Horizontal scaling

4. **Web Interface**
   - GUI configuration
   - Real-time monitoring
   - Result visualization

5. **Additional Databases**
   - MySQL support
   - Oracle support
   - MongoDB support

---

## 11. Glossary

| Term | Definition |
|------|-----------|
| **pg_ozerki** | PostgreSQL extension for schema and statistics export |
| **DataGen** | Java application for synthetic data generation |
| **Schema** | Database structure (tables, columns, indexes) |
| **Statistics** | Information about data distribution in tables |
| **Synthetic Data** | Artificially generated data mimicking real data patterns |
| **Batch** | Group of records processed together |
| **FK/PK** | Foreign Key / Primary Key constraints |
| **Topological Sort** | Algorithm to order items based on dependencies |
| **Connection Pool** | Reusable database connections (HikariCP) |
| **CSV** | Comma-separated values file format |

---

**Document Version:** 1.0  
**Last Updated:** 2026-05-26  
**Branch:** index-export
