# contrib/postgis_toaster/Makefile

MODULE_big = postgis_toaster
OBJS = \
	$(WIN32RES) \
	postgis_toaster.o

EXTENSION = postgis_toaster
DATA = postgis_toaster--1.0.sql
PGFILEDESC = "postgis_toaster - postgis toaster"

REGRESS = postgis_toaster

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/postgis_toaster
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
