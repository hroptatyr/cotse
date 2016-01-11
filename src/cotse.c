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

	/* scratch array, row wise and column wise */
	uint64_t *row_scratch;
	size_t j;

	/* provision for NSAMP timestamps and tags */
	cots_to_t t[NSAMP];
	cots_tag_t m[NSAMP];
};

static const char nul_layout[] = "";


static void
_evict_scratch(struct _ts_s *_s)
{
	const size_t nflds = _s->public.nfields;
	size_t z;

	z = comp_to((uint8_t*)_s->t, _s->t, _s->j);
	fprintf(stderr, "toff %zu -> %zu\n", _s->j * sizeof(*_s->t), z);

	z = comp_tag((uint8_t*)_s->m, _s->m, _s->j);
	fprintf(stderr, "mtrs %zu -> %zu\n", _s->j * sizeof(*_s->t), z);

	/* columnarise */
	for (size_t i = 0U; i < nflds; i++) {
		switch (_s->public.layout[i]) {
			uint64_t _col[NSAMP + NSAMP / 2U];

		case COTS_LO_PRC:
		case COTS_LO_FLT: {
			uint32_t *c = (void*)_col;
			for (size_t j = 0U; j < _s->j; j++) {
				c[j] = _s->row_scratch[j * nflds + i];
			}

			z = comp_px((uint8_t*)c, c, _s->j);
			fprintf(stderr, "prcs %zu -> %zu\n",
				_s->j * sizeof(*c), z);
			break;
		}
		case COTS_LO_QTY:
		case COTS_LO_DBL: {
			uint64_t *c = (void*)_col;
			for (size_t j = 0U; j < _s->j; j++) {
				c[j] = _s->row_scratch[j * nflds + i];
			}

			z = comp_qx((uint8_t*)c, c, _s->j);
			fprintf(stderr, "qtys %zu -> %zu\n",
				_s->j * sizeof(*c), z);
			break;
		}
		default:
			break;
		}
	}

	_s->j = 0U;
	return;
}


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

		res->row_scratch = calloc(nflds * NSAMP, sizeof(uint64_t));
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
	if (LIKELY(_ts->row_scratch != NULL)) {
		free(_ts->row_scratch);
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
	struct _ts_s *_s = (void*)s;
	va_list vap;

	_s->t[_s->j] = t;
	_s->m[_s->j] = m;
	va_start(vap, m);
	for (size_t i = 0U, n = _s->public.nfields,
		     row = _s->j * n; i < n; i++) {
		switch (_s->public.layout[i]) {
		case COTS_LO_PRC:
		case COTS_LO_FLT:
			_s->row_scratch[row + i] = va_arg(vap, uint32_t);
			break;
		case COTS_LO_QTY:
		case COTS_LO_DBL:
			_s->row_scratch[row + i] = va_arg(vap, uint64_t);
			break;
		default:
			break;
		}
	}
	va_end(vap);

	if (UNLIKELY(++_s->j == NSAMP)) {
		/* auto-eviction */
		_evict_scratch(_s);
	}
	return 0;
}

int
cots_write_tick(cots_ts_t s, const struct cots_tick_s *data)
{
	struct _ts_s *_s = (void*)s;
	const size_t nflds = _s->public.nfields;

	_s->t[_s->j] = data->toff;
	_s->m[_s->j] = data->tag;

	memcpy(&_s->row_scratch[_s->j * nflds],
	       data->value, nflds * sizeof(uint64_t));

	if (UNLIKELY(++_s->j == NSAMP)) {
		/* auto-eviction */
		_evict_scratch(_s);
	}
	return 0;
}

/* cotse.c ends here */
