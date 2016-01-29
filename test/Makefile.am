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
AM_CPPFLAGS += -DTEST
AM_CPPFLAGS += -D__STDC_WANT_DEC_FP__
AM_CPPFLAGS += $(cotse_CFLAGS)
AM_LDFLAGS = $(cotse_LIBS)

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


check_PROGRAMS += creat_01
TESTS += creat_01
TESTS += creat_01.clit

.NOTPARALLEL:

## Makefile.am ends here
