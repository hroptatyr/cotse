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

static char *line;
static size_t llen;


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
dump(cots_ss_t hdl, const struct cots_tsoa_s *cols, size_t n)
{
	for (size_t i = 0U; i < n; i++) {
		char *lp = line;
#define lz	(size_t)(line + llen - lp)
		cots_to_t t = cols->toffs[i];
		size_t j = 0U;

		goto ini_tim;

		for (; j < hdl->nfields; j++) {
			if (UNLIKELY(lz < llen / 4U)) {
				/* not enough breathing space */
				line = realloc(line, llen *= 2U);
			}
			switch (hdl->layout[j]) {
			case COTS_LO_PRC: {
				cots_px_t *pp = cols->cols[j];

				*lp++ = '\t';
				if (LIKELY(pp[i] != COTS_PX_MISS.d32)) {
					lp += d32tostr(lp, lz, pp[i]);
				}
				break;
			}
			case COTS_LO_QTY: {
				cots_qx_t *qp = cols->cols[j];

				*lp++ = '\t';
				if (LIKELY(qp[i] != COTS_QX_MISS.d64)) {
					lp += d64tostr(lp, lz, qp[i]);
				}
				break;
			}
			case COTS_LO_TIM: {
				cots_to_t *tp = cols->cols[j];

				t = tp[i];
				*lp++ = '\t';

			ini_tim:
				lp += snprintf(
					lp, lz, "%lu.%09lu",
					t / 1000000000U, t % 1000000000U);
				break;
			}
			case COTS_LO_CNT:
			case COTS_LO_TAG:
			case COTS_LO_SIZ: {
				uint64_t *zp = cols->cols[j];

				*lp++ = '\t';
				lp += snprintf(lp, lz, "%lu", zp[i]);
				break;
			}
			default:
				*lp++ = '\t';
				*lp++ = 'U';
				*lp++ = 'N';
				*lp++ = 'K';
				break;
			}
		}
		*lp++ = '\n';
		fwrite(line, 1U, lp - line, stdout);
	}
	return;
}


int
main(int argc, char *argv[])
{
	int rc = 0;

	/* get some line buffer */
	if ((line = malloc(llen = 256U)) == NULL) {
		/* don't even start */
		return 1;
	}

	for (int i = 1; i < argc; i++) {
		cots_ss_t hdl;
		ssize_t n;

		if ((hdl = cots_open_ss(argv[i], O_RDONLY)) == NULL) {
			serror("Error: cannot open file `%s'", argv[i]);
			rc = 1;
			continue;
		}

		/* use a generic tsoa */
		with (void *_cols[hdl->nfields + 1U], *cols = (void*)_cols) {
			cots_init_tsoa(cols, hdl);
			while ((n = cots_read_ticks(cols, hdl)) > 0) {
				fprintf(stderr, "got %zd ticks\n", n);
				dump(hdl, cols, n);
			}
			fprintf(stderr, "%p  %zd\n", hdl, n);
			cots_fini_tsoa(cols, hdl);
		}
		cots_close_ss(hdl);
	}
	free(line);
	return rc;
}

/* cotsdump.c ends here */
