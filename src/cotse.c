/*** cotse.c -- cotse API
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
#include "cotse.h"
#include "comp-to.h"
#include "comp-px.h"
#include "comp-qx.h"
#include "comp-ob.h"
#include "intern.h"
#include "nifty.h"

#include <stdio.h>

#define NSAMP		(8192U)

struct _ts_s {
	struct cots_ts_s public;
	/* compacted version of fields */
	char *fields;

	cots_ob_t obarray;
};

static const char nul_layout[] = "";


/* public API */
cots_ts_t
make_cots_ts(const char *layout)
{
	struct _ts_s *res = calloc(1, sizeof(*res));

	if (LIKELY(layout != NULL)) {
		res->public.layout = strdup(layout);
	} else {
		res->public.layout = nul_layout;
	}

	with (size_t nflds = strlen(res->public.layout)) {
		void *nfp = deconst(&res->public.nfields);
		memcpy(nfp, &nflds, sizeof(nflds));
	}
	res->obarray = make_cots_ob();
	return (cots_ts_t)res;
}

void
free_cots_ts(cots_ts_t ts)
{
	struct _ts_s *_ts = (void*)ts;

	if (LIKELY(_ts->public.layout != nul_layout)) {
		free(deconst(_ts->public.layout));
	}
	if (_ts->public.fields != NULL) {
		free(deconst(_ts->public.fields));
		free(_ts->fields);
	}
	free_cots_ob(_ts->obarray);
	free(_ts);
	return;
}


cots_tag_t
cots_tag(cots_ts_t s, const char *str, size_t len)
{
	struct _ts_s *_s = (void*)s;
	return cots_intern(_s->obarray, str, len);
}


int
cots_put_fields(cots_ts_t s, const char **fields)
{
	struct _ts_s *_s = (void*)s;
	const size_t nfields = _s->public.nfields;
	const char *const *old = _s->public.fields;
	const char **new = calloc(nfields + 1U, sizeof(*new));
	char *flds;

	if (UNLIKELY(new == NULL)) {
		return -1;
	}
	for (size_t i = 0U; i < nfields; i++) {
		const size_t this = strlen(fields[i]);
		const size_t prev = (uintptr_t)new[i];
		new[i + 1U] = (void*)(uintptr_t)(prev + this + 1U/*\nul*/);
	}
	/* make the big compacted array */
	with (size_t ztot = (uintptr_t)new[nfields], o = 0U) {
		flds = calloc(ztot, sizeof(*flds));

		if (UNLIKELY(flds == NULL)) {
			free(new);
			return -1;
		}

		/* compactify */
		for (size_t i = 0U; i < nfields; i++) {
			const size_t len = (uintptr_t)new[i + 1U] - o;
			memcpy(flds + o, fields[i], len);
			o += len;
		}

		/* update actual field pointers in new */
		for (size_t i = 0U; i < nfields; i++) {
			new[i] = flds + (uintptr_t)new[i];
		}
		new[nfields] = NULL;
	}
	/* swap old for new */
	_s->public.fields = new;
	if (UNLIKELY(old != NULL)) {
		free(deconst(old));
		free(_s->fields);
	}
	_s->fields = flds;
	return 0;
}


int
cots_write_va(cots_ts_t s, cots_to_t t, cots_tag_t m, ...)
{
	static cots_to_t toff[NSAMP];
	static cots_tag_t mtrs[NSAMP];
	static cots_px_t prcs[NSAMP];
	static cots_qx_t qtys[NSAMP];
	static size_t isamp;
	va_list vap;

	toff[isamp] = t;
	mtrs[isamp] = m;
	va_start(vap, m);
	for (const char *lp = s->layout; *lp; lp++) {
		switch (*lp) {
		case 'p':
			prcs[isamp] = va_arg(vap, cots_px_t);
			break;
		case 'q':
			qtys[isamp] = va_arg(vap, cots_qx_t);
			break;
		default:
			break;
		}
	}
	va_end(vap);

	if (UNLIKELY(++isamp == countof(toff))) {
		static uint8_t data[sizeof(toff)];
		size_t z;

		z = comp_to(data, toff, countof(toff));
		fprintf(stderr, "toff %zu -> %zu\n", sizeof(toff), z);

		z = comp_tag(data, mtrs, countof(mtrs));
		fprintf(stderr, "mtrs %zu -> %zu\n", sizeof(mtrs), z);

		z = comp_px(data, prcs, countof(prcs));
		fprintf(stderr, "prcs %zu -> %zu\n", sizeof(prcs), z);

		z = comp_qx(data, qtys, countof(qtys));
		fprintf(stderr, "qtys %zu -> %zu\n", sizeof(qtys), z);

		isamp = 0U;
	}
	return 0;
}

int
cots_write_tick(cots_ts_t s, const struct cots_tick_s *data)
{
	static uint64_t vals[NSAMP * 4U];
	static size_t isamp;
	struct _ts_s *_s = (void*)s;
	const size_t nflds = _s->public.nfields + 2U;

	memcpy(vals + nflds * isamp, data, nflds * sizeof(*vals));
	if (UNLIKELY(++isamp == NSAMP)) {
		static uint8_t page[sizeof(vals)];
		cots_to_t t[NSAMP];
		cots_tag_t m[NSAMP];
		uint32_t p[NSAMP];
		uint64_t q[NSAMP];
		size_t z;

		for (size_t i = 0U; i < NSAMP; i++) {
			t[i] = vals[nflds * i + 0U];
			m[i] = vals[nflds * i + 1U];
			p[i] = (uint32_t)vals[nflds * i + 2U];
			q[i] = vals[nflds * i + 3U];
		}

		z = comp_to(page, t, countof(t));
		fprintf(stderr, "toff %zu -> %zu\n", sizeof(t), z);

		z = comp_tag(page, m, countof(m));
		fprintf(stderr, "mtrs %zu -> %zu\n", sizeof(m), z);

		z = comp_px(page, p, countof(p));
		fprintf(stderr, "prcs %zu -> %zu\n", sizeof(p), z);

		z = comp_qx(page, q, countof(q));
		fprintf(stderr, "qtys %zu -> %zu\n", sizeof(q), z);

		isamp = 0U;
	}
	return 0;
}

/* cotse.c ends here */
