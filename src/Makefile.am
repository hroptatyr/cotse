### Makefile.am
include $(top_builddir)/version.mk

AM_CFLAGS = $(EXTRA_CFLAGS)
AM_CPPFLAGS = -D_POSIX_C_SOURCE=201001L -D_XOPEN_SOURCE=700 -D_BSD_SOURCE
AM_CPPFLAGS += -DHAVE_VERSION_H
AM_CPPFLAGS += -D__STDC_WANT_DEC_FP__ -DHAVE_DFP754_BID_LITERALS
AM_LDFLAGS = $(XCCLDFLAGS)

bin_PROGRAMS =
noinst_PROGRAMS =
lib_LTLIBRARIES =
noinst_LTLIBRARIES =
pkglib_LTLIBRARIES =
noinst_HEADERS =
BUILT_SOURCES =
EXTRA_DIST = $(BUILT_SOURCES)
CLEANFILES = 
SUFFIXES =

CLEANFILES += version.c
EXTRA_DIST += version.c.in

include_HEADERS = cotse.h

lib_LTLIBRARIES += libcotse.la
libcotse_la_SOURCES = cotse.c cotse.h
libcotse_la_SOURCES += nifty.h
libcotse_la_SOURCES += pfor.c pfor.h
libcotse_la_SOURCES += bitpack.c bitpack.h bitpack64.h
libcotse_la_SOURCES += comp.c comp.h
libcotse_la_SOURCES += comp-to.c comp-to.h
libcotse_la_SOURCES += comp-px.c comp-px.h
libcotse_la_SOURCES += comp-qx.c comp-qx.h
libcotse_la_SOURCES += comp-ob.c comp-ob.h
libcotse_la_SOURCES += hash.c hash.h
libcotse_la_SOURCES += intern.c intern.h
libcotse_la_CPPFLAGS = $(AM_CPPFLAGS)
libcotse_la_CPPFLAGS += -D_GNU_SOURCE

noinst_PROGRAMS += bla
bla_SOURCES = bla.c
bla_SOURCES += dfp754_d32.c dfp754_d32.h
bla_SOURCES += dfp754_d64.c dfp754_d64.h
bla_CPPFLAGS = $(AM_CPPFLAGS) -D_GNU_SOURCE
bla_LDFLAGS = $(AM_LDFLAGS) -lm -static
bla_LDADD = libcotse.la


## version rules
version.c: version.c.in $(top_builddir)/.version
	$(AM_V_GEN) PATH="$(top_builddir)/build-aux:$${PATH}" \
		yuck$(EXEEXT) scmver --ignore-noscm --force -o $@ \
			--use-reference --reference $(top_builddir)/.version $<

## yuck rule
SUFFIXES += .yuck
SUFFIXES += .yucc
.yuck.yucc:
	$(AM_V_GEN) PATH=$(top_builddir)/build-aux:"$${PATH}" \
		yuck$(EXEEXT) gen -o $@ $<

## Create preprocessor output (debugging purposes only)
.c.i:
	$(COMPILE) -E -o $@ $<

## Create assembler output (debugging purposes only)
.c.s:
	$(COMPILE) -S -c $(AM_CFLAGS) $<
