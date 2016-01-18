/*** cotspump.c -- simple database pumping tool
 *
 * Copyright (C) 2014-2016 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
 *
 * This file is part of cotse.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***/
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include "dfp754_d32.h"
#include "dfp754_d64.h"
#include "cotse.h"
#include "nifty.h"

#define NSECS	(1000000000UL)
#define MSECS	(1000UL)
#define MSECQ	(1000000UL)

#define strtopx		strtod32
#define strtoqx		strtod64

struct samp_s {
	cots_to_t t;
	cots_tag_t m;
	cots_px_t b;
	cots_qx_t q;
};


static __attribute__((format(printf, 1, 2))) void
serror(const char *fmt, ...)
{
#define uerror	errno, serror
	va_list vap;
	va_start(vap, fmt);
	vfprintf(stderr, fmt, vap);
	va_end(vap);
	if (errno) {
		fputc(':', stderr);
		fputc(' ', stderr);
		fputs(strerror(errno), stderr);
	}
	fputc('\n', stderr);
	return;
}

static struct samp_s
push(cots_ts_t ts, const char *line, size_t UNUSED(llen))
{
	char *on;
	long unsigned int s, x;
	cots_px_t b;
	cots_qx_t q;
	cots_tag_t m;

	if (line[20U] != '\t') {
		return (struct samp_s){0U, 0U};
	} else if (!(s = strtoul(line, &on, 10))) {
		return (struct samp_s){0U, 0U};
	} else if (*on++ != '.') {
		return (struct samp_s){0U, 0U};
	} else if ((x = strtoul(on, &on, 10), *on != '\t')) {
		return (struct samp_s){0U, 0U};
	}
	s = (s * NSECS + x);

	with (const char *ecn = ++on) {
		if (UNLIKELY((on = strchr(ecn, '\t')) == NULL)) {
			return (struct samp_s){0U, 0U};
		} else if (UNLIKELY(!(m = cots_tag(ts, ecn, on - ecn)))) {
			/* fuck */
			return (struct samp_s){0U, 0U};
		}
	}

	if (*++on != '\t') {
		b = strtopx(on, &on);
	} else {
		b = COTS_PX_MISS.d32;
	}

	if (*++on != '\n') {
		q = strtoqx(on, &on);
	} else {
		q = COTS_QX_MISS.d64;
	}
	return (struct samp_s){s, m, b, q};
}


int
main(int argc, char *argv[])
{
	cots_ts_t db;
	int rc = 0;
	char *line = NULL;
	size_t llen = 0U;

	if ((db = make_cots_ts("tmpq", 0U)) == NULL) {
		return 1;
	}
	cots_put_fields(db, (const char*[]){"STAMP", "SOURCE", "BID", "SIZE"});

	with (const char *fn = argv[1U]) {
		size_t i = 0U;

		if (UNLIKELY(fn == NULL)) {
			uerror("Warning: no file specified");
		} else if (cots_attach(db, fn, O_CREAT | O_RDWR) < 0) {
			serror("Error: cannot attach `%s'", fn);
			rc = 1;
			break;
		}

		for (ssize_t nrd; (nrd = getline(&line, &llen, stdin)) > 0;) {
			struct samp_s x = push(db, line, nrd);

			if (x.m) {
				cots_write_tick(db, &x);
			}
			if (++i >= 1000000) {
				break;
			}
		}
	}
	free(line);
	free_cots_ts(db);
	return rc;
}

/* cotspump.c ends here */
