# Help the Developers and yourself. Just use the C locale and settings
# for the compilation. They can still be overriden by make LANG=<whatever>
# but that is general a not very good idea
include $(top_builddir)/version.mk

LANG = C
LC_ALL = C

cotse_CFLAGS = -I$(top_srcdir)/src
cotse_LIBS = $(top_builddir)/src/libcotse.la

AM_CFLAGS = $(EXTRA_CFLAGS)
AM_CPPFLAGS = -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600 -D_BSD_SOURCE
AM_CPPFLAGS += -DTESTING
AM_CPPFLAGS += -D__STDC_WANT_DEC_FP__
AM_CPPFLAGS += -DMUNIT_DISABLE_TIMING
AM_CPPFLAGS += $(cotse_CFLAGS)
AM_LDFLAGS = $(cotse_LIBS) -lm

EXTRA_DIST = $(BUILT_SOURCES) $(TESTS)
TESTS =
TEST_EXTENSIONS =
BUILT_SOURCES =
dt_tests =
bin_tests =
batch_tests =
check_PROGRAMS =
CLEANFILES = $(check_PROGRAMS)
noinst_LIBRARIES =

## summon our helper
include clitoris.am
AM_CLIT_LOG_FLAGS = -v --builddir $(top_builddir)/src
TESTS_ENVIRONMENT = root=$(top_srcdir)


check_PROGRAMS += cotse
cotse_SOURCES = cotse.c
cotse_SOURCES += munit.c munit.h
cotse_CPPFLAGS = $(AM_CPPFLAGS) -D_GNU_SOURCE
TESTS += cotse


check_PROGRAMS += creat_01
TESTS += creat_01.clit

check_PROGRAMS += creat_02
TESTS += creat_02.clit

check_PROGRAMS += open_twice_01
TESTS += open_twice_01.clit

check_PROGRAMS += appnd_01
TESTS += appnd_01.clit


cotse.c: $(top_srcdir)/src/cotse.c
	$(LN_S) $< $@

## Makefile.am ends here
