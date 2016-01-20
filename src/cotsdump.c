/*** cotsdump.c -- simple database dumping tool
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

struct samp_s {
	struct cots_tsoa_s proto;
	cots_tag_t *m;
	cots_px_t *b;
	cots_qx_t *q;
};


static __attribute__((format(printf, 1, 2))) void
serror(const char *fmt, ...)
{
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

static void
dump(const struct samp_s s, size_t n)
{
	for (size_t i = 0U; i < n; i++) {
		cots_to_t t = s.proto.toffs[i];
		char p[32U];
		char q[64U];

		d32tostr(p, sizeof(p), s.b[i]);
		d64tostr(q, sizeof(q), s.q[i]);

		printf("%lu.%09lu\t%lu\t%s\t%s\n",
		       t / 1000000000U, t % 1000000000U, s.m[i], p, q);
	}
	return;
}


int
main(int argc, char *argv[])
{
	int rc = 0;

	for (int i = 1; i < argc; i++) {
		cots_ss_t hdl;
		struct samp_s s;
		ssize_t n;

		if ((hdl = cots_open_ss(argv[i], O_RDONLY)) == NULL) {
			serror("Error: cannot open file `%s'", argv[i]);
			rc = 1;
			continue;
		}

		while ((n = cots_read_ticks(&s.proto, hdl)) > 0) {
			fprintf(stderr, "got %zd ticks\n", n);
			dump(s, n);
		}
		fprintf(stderr, "%p\n", hdl);
		cots_close_ss(hdl);
	}
	return rc;
}

/* cotsdump.c ends here */
