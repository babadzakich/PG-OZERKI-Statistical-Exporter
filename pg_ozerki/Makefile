MODULE_big = pg_ozerki
OBJS = anyarray_elemtype.o $(WIN32RES) schema_dump.o ozerki_utils.o ddl_query.o

EXTENSION = pg_ozerki
DATA = pg_ozerki--1.0.sql

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_ozerki
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
