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
#include <sys/mman.h>
#include <fcntl.h>
#include "cotse.h"
#include "comp.h"
#include "intern.h"
#include "nifty.h"

#include <stdio.h>

#define MAP_MEM		(MAP_SHARED | MAP_ANON)
#define PROT_MEM	(PROT_READ | PROT_WRITE)
#ifndef MAP_ANON
# define MAP_ANON	MAP_ANONYMOUS
#endif	/* !MAP_ANON */

#define NSAMP		(8192U)
#define NTIDX		(512U)

struct _ts_s {
	struct cots_ts_s public;
	/* compacted version of fields */
	char *fields;

	cots_ob_t obarray;

	/* scratch array, row wise */
	uint64_t *row_scratch;
	size_t j;

	/* provision for NSAMP timestamps and tags */
	cots_to_t t[NSAMP];
	cots_tag_t m[NSAMP];

	/* currently attached file */
	int fd;

	cots_to_t tidx[NTIDX];
	uint8_t *pidx[NTIDX];
	size_t k;
};

struct fhdr_s {
	uint8_t magic[4U];
	uint8_t version[2U];
	uint16_t endian;
	uint64_t flags;
	/* offset to index pages */
	uint64_t ioff;
	/* offset to wal */
	uint64_t woff;
	/* layout, \nul term'd */
	uint8_t layout[];
};

struct idx_page_s {
	/* time-offsets */
	cots_to_t tidx[NTIDX];
	/* block sizes */
	uint32_t zblk[NTIDX];
};

static const char nul_layout[] = "";


static size_t
mmap_pgsz(void)
{
	static size_t pgsz;

	if (UNLIKELY(!pgsz)) {
		pgsz = sysconf(_SC_PAGESIZE);
	}
	return pgsz;
}

static void*
mmap_any(int fd, int prot, int flags, off_t off, size_t len)
{
	const size_t pgsz = mmap_pgsz();
	size_t ofp = off / pgsz, ofi = off % pgsz;
	char *p = mmap(NULL, len + ofi, prot, flags, fd, ofp * pgsz);
	return LIKELY(p != MAP_FAILED) ? p + ofi : NULL;
}

static void
munmap_any(void *map, off_t off, size_t len)
{
	size_t pgsz = mmap_pgsz();
	size_t ofi = off % pgsz;
	munmap((char*)map - ofi, len + ofi);
	return;
}

static void
wr_hdr(const struct _ts_s *_s)
{
	const size_t nflds = _s->public.nfields;
	struct fhdr_s h = {
		"cots", "v0", 0x3c3eU, 0ULL, 0ULL, 0ULL,
	};
	write(STDOUT_FILENO, &h, sizeof(h));
	write(STDOUT_FILENO, _s->public.layout, nflds + 1U);

	if (_s->public.fields) {
		const char *last = _s->public.fields[nflds - 1U];
		size_t zfld = last + strlen(last) - _s->fields;
		write(STDOUT_FILENO, _s->fields, zfld + 1U);
	}
	return;
}

static int
_evict_scratch(struct _ts_s *_s)
{
	const size_t nflds = _s->public.nfields;
	const char *const layo = _s->public.layout;
	const uint64_t *rows = _s->row_scratch;
	const size_t zrow = (nflds + 2U) * sizeof(*_s->row_scratch);
	const size_t bsz = zrow * 3U * _s->j / 2U;
	uint8_t *buf;
	size_t z;

	if (UNLIKELY(!_s->j)) {
		return 0;
	}
	buf = mmap(NULL, bsz, PROT_MEM, MAP_MEM, -1, 0);
	if (UNLIKELY(buf == MAP_FAILED)) {
		return -1;
	}

	/* call the compactor */
	z = comp(buf + sizeof(z), nflds, _s->j, layo, _s->t, _s->m, rows);
	memcpy(buf, &z, sizeof(z));
	z += sizeof(z);

	size_t uncomp = _s->j * 2U * sizeof(uint64_t) + 12U * _s->j;
	fprintf(stderr, "comp %zu  (%0.2f%%)\n", z, 100. * (double)z / (double)uncomp);

	if (!_s->k) {
		wr_hdr(_s);
	};
	/* write data */
	write(STDOUT_FILENO, buf, z);
	fsync(STDOUT_FILENO);

	/* store in index */
	_s->tidx[_s->k + 0U] = _s->t[0U];
	/* best effort to guess the next index's timestamp */
	_s->tidx[_s->k + 1U] = _s->t[_s->j - 1U];
	/* store pointer */
	_s->pidx[_s->k + 0U] = mremap(buf, bsz, z, MREMAP_MAYMOVE);
	_s->pidx[_s->k + 1U] = _s->pidx[_s->k + 0U] + z;
	_s->k++;

	_s->j = 0U;
	return -1;
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

	/* use a backing file */
	return (cots_ts_t)res;
}

void
free_cots_ts(cots_ts_t ts)
{
	struct _ts_s *_ts = (void*)ts;

	if (UNLIKELY(_ts->j)) {
		_evict_scratch(_ts);
	}
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
	if (LIKELY(_ts->obarray != NULL)) {
		free_cots_ob(_ts->obarray);
	}
	free(_ts);
	return;
}

cots_ts_t
cots_open_ts(const char *file, int flags)
{
	struct _ts_s *res;
	struct fhdr_s hdr;
	int fd;

	if ((fd = open(file, flags ? O_RDWR : O_RDONLY)) < 0) {
		return NULL;
	}
	/* read header bit */
	if (read(fd, &hdr, sizeof(hdr)) < (ssize_t)sizeof(hdr)) {
		close(fd);
		return NULL;
	}

	if (UNLIKELY((res = calloc(1, sizeof(*res))) == NULL)) {
		return NULL;
	}
	/* make backing file known */
	res->fd = fd;
	res->public.filename = strdup(file);
	{
		size_t zlay = 8U, olay = 0U;
		char *layo = malloc(8U);
		char *eo;

		while (1) {
			read(fd, layo + olay, zlay);
			if ((eo = memchr(layo + olay, '\0', zlay)) != NULL) {
				break;
			}
			/* otherwise double in size and retry */
			olay = zlay;
			layo = realloc(layo, zlay * 2U);
		}
		res->public.layout = layo;

		with (size_t nflds = eo - layo) {
			void *nfp = deconst(&res->public.nfields);
			memcpy(nfp, &nflds, sizeof(nflds));
		}
	}

	with (size_t nflds = strlen(res->public.layout)) {
		void *nfp = deconst(&res->public.nfields);
		memcpy(nfp, &nflds, sizeof(nflds));

		res->row_scratch = calloc(nflds * NSAMP, sizeof(uint64_t));
	}

	/* use a backing file */
	return (cots_ts_t)res;
}

int
cots_close_ts(cots_ts_t s)
{
	struct _ts_s *_s = (void*)s;

	if (LIKELY(_s->fd >= 0)) {
		close(_s->fd);
	}
	free_cots_ts(s);
	return 0;
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

ssize_t
cots_read_ticks(struct cots_tsoa_s *tsoa, cots_ts_t s)
{
	struct _ts_s *_s = (void*)s;
	size_t n = NSAMP;

	tsoa->toffs = _s->t;
	tsoa->tags = _s->m;

	for (size_t i = 0U, nflds = _s->public.nfields; i < nflds; i++) {
		tsoa->more[i] = _s->row_scratch + i * n;
	}
	return n;
}

/* cotse.c ends here */
