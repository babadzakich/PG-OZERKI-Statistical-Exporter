#-------------------------------------------------------------------------
#
# Makefile for src/bin/pg_ozerki
#
# Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
# Portions Copyright (c) 1994, Regents of the University of California
#
# src/bin/pg_ozerki/Makefile
#
#-------------------------------------------------------------------------

PGFILEDESC = "pg_ozerki - export application for pg_ozerki util for PostgreSQL databases"
PGAPPICON=win32

subdir = src/bin/pg_ozerki
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

export GZIP_PROGRAM=$(GZIP)
export LZ4
export ZSTD
export with_icu

override CPPFLAGS := -I$(libpq_srcdir) $(CPPFLAGS)
LDFLAGS_INTERNAL += -L$(top_builddir)/src/fe_utils -lpgfeutils $(libpq_pgport)

OBJS = \
	$(WIN32RES) \
	ozerki_utils.o \
	get_schema.o \
	sort.o \
	dump.o \
	query.o \
	stat_export.o \
	plan_export.o \
	constraint_export.o


CYAML_CFLAGS = $(shell pkg-config --cflags libcyaml)
CYAML_LIBS = $(shell pkg-config --libs libcyaml)

override CPPFLAGS := $(CYAML_CFLAGS) -I$(libpq_srcdir) $(CPPFLAGS)

LIBS += $(CYAML_LIBS)

all: pg_ozerki

pg_ozerki: pg_ozerki.o $(OBJS) | submake-libpq submake-libpgport submake-libpgfeutils
	$(CC) $(CFLAGS) pg_ozerki.o $(OBJS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o $@$(X)

install: all installdirs
	$(INSTALL_PROGRAM) pg_ozerki$(X) '$(DESTDIR)$(bindir)'/pg_ozerki$(X)

installdirs:
	$(MKDIR_P) '$(DESTDIR)$(bindir)'

check:
	$(prove_check)

installcheck:
	$(prove_installcheck)

uninstall:
	rm -f $(addprefix '$(DESTDIR)$(bindir)'/, pg_ozerki$(X))


clean distclean:
	rm -f pg_ozerki$(X) $(OBJS) pg_ozerki.o
	rm -rf tmp_check