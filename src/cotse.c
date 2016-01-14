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
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "cotse.h"
#include "comp.h"
#include "intern.h"
#include "boobs.h"
#include "nifty.h"

#include <stdio.h>

#define MAP_MEM		(MAP_SHARED | MAP_ANON)
#define PROT_MEM	(PROT_READ | PROT_WRITE)
#ifndef MAP_ANON
# define MAP_ANON	MAP_ANONYMOUS
#endif	/* !MAP_ANON */

#define NSAMP		(8192U)
#define NTIDX		(512U)

#define ALGN16(x)	((uintptr_t)((x) + 0xfU) & ~0xfULL)

/* file header, mmapped for convenience */
struct fhdr_s {
	uint8_t magic[4U];
	uint8_t version[2U];
	uint16_t endian;
	uint64_t flags;
	/* offset to index pages */
	uint64_t ioff;
	/* offset to obarray */
	uint64_t ooff;
	/* layout, \nul term'd */
	uint8_t layout[];
};

struct idx_s {
	/* time-offsets */
	cots_to_t t[NTIDX];
	/* offset into file */
	uint64_t z[NTIDX];
};

struct blob_s {
	size_t z;
	uint8_t *data;
	cots_to_t from;
	cots_to_t till;
};

struct _ts_s {
	struct cots_ts_s public;

	/* mmapped file header */
	struct fhdr_s *mdr;

	/* compacted version of fields */
	char *fields;

	cots_ob_t obarray;

	/* scratch array, row wise */
	uint64_t *row_scratch;
	/* number of rows in the scratch buffer */
	size_t nrows;

	/* provision for NSAMP timestamps and tags */
	cots_to_t t[NSAMP];
	cots_tag_t m[NSAMP];

	/* currently attached file and its opening flags */
	int fd;
	int fl;

	struct idx_s root;
	/* cache pointers to already mapped regions */
	uint8_t *pidx[NTIDX];
	/* number of indices */
	size_t nidx;
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
	uint8_t *p = mmap(NULL, len + ofi, prot, flags, fd, ofp * pgsz);
	return LIKELY(p != MAP_FAILED) ? p + ofi : NULL;
}

static int
munmap_any(void *map, off_t off, size_t len)
{
	size_t pgsz = mmap_pgsz();
	size_t ofi = off % pgsz;
	uint8_t *omp = (uint8_t*)map - ofi;

	omp = !((uintptr_t)omp & (pgsz - 1U)) ? omp : map;
	len = !((uintptr_t)omp & (pgsz - 1U)) ? len + ofi : len;
	return munmap(omp, len);
}

static int
mprot_any(void *map, off_t off, size_t len, int prot)
{
	size_t pgsz = mmap_pgsz();
	size_t ofi = off % pgsz;
	uint8_t *omp = (uint8_t*)map - ofi;

	omp = !((uintptr_t)omp & (pgsz - 1U)) ? omp : map;
	len = !((uintptr_t)omp & (pgsz - 1U)) ? len + ofi : len;
	return mprotect(omp, len, prot);
}

static int
msync_any(void *map, off_t off, size_t len, int flags)
{
	size_t pgsz = mmap_pgsz();
	size_t ofi = off % pgsz;
	uint8_t *omp = (uint8_t*)map - ofi;

	omp = !((uintptr_t)omp & (pgsz - 1U)) ? omp : map;
	len = !((uintptr_t)omp & (pgsz - 1U)) ? len + ofi : len;
	return msync(omp, len, flags);
}


static inline off_t
_ioff(const struct _ts_s *_s)
{
/* calculate index offset, that's header size + blob size + alignment */
	return ALGN16(_s->root.z[_s->nidx]);
}

static inline size_t
_ilen(const struct _ts_s *_s)
{
/* calculate index length */
	return (_s->nidx + 1U) * (sizeof(*_s->root.t) + sizeof(*_s->root.z));
}

static struct blob_s
_make_blob(const char *flds, size_t nflds, size_t nrows,
	   const cots_to_t *t, const cots_tag_t *m, const uint64_t *rows)
{
	const size_t zrow = (nflds + 2U) * sizeof(*rows);
	void *cols[nflds];
	uint8_t *buf;
	size_t bsz;
	size_t z;

	if (UNLIKELY(!nrows)) {
		/* trivial */
		return (struct blob_s){0U, NULL};
	}

	/* trial mmap */
	bsz = 3U * zrow * nrows / 2U;
	buf = mmap(NULL, bsz, PROT_MEM, MAP_MEM, -1, 0);
	if (buf == MAP_FAILED) {
		return (struct blob_s){0U, NULL};
	}

	/* columnarise */
	for (size_t i = 0U,
		     /* column index with some breathing space in bytes */
		     bi = 3U * 2U * sizeof(*rows) * nrows / 2U;
	     i < nflds; i++) {
		cols[i] = (void*)ALGN16(buf + bi + sizeof(z));

		switch (flds[i]) {
		case COTS_LO_PRC:
		case COTS_LO_FLT: {
			uint32_t *c = cols[i];

			for (size_t j = 0U; j < nrows; j++) {
				c[j] = rows[j * nflds + i];
			}
			bi += nrows * sizeof(*c);
			break;
		}
		case COTS_LO_QTY:
		case COTS_LO_DBL: {
			uint64_t *c = cols[i];

			for (size_t j = 0U; j < nrows; j++) {
				c[j] = rows[j * nflds + i];
			}
			bi += nrows * sizeof(*c);
			break;
		}
		default:
			break;
		}
	}

	/* call the compactor */
	z = comp(buf + sizeof(z), nflds, nrows, flds, t, m, cols);
	memcpy(buf, &z, sizeof(z));
	z += sizeof(z);

	/* make the map a bit tinier */
	with (uint8_t *blo = mremap(buf, bsz, z, MREMAP_MAYMOVE)) {
		if (UNLIKELY(blo == MAP_FAILED)) {
			/* what a pity */
			goto mun_out;
		}
		buf = blo;
	}
	return (struct blob_s){z, buf, t[0U], t[nrows - 1U]};

mun_out:
	munmap(buf, bsz);
	return (struct blob_s){0U, NULL};
}

static int
_add_blob(struct _ts_s *_s, struct blob_s b)
{
	if (_s->fd >= 0) {
		/* backing file present */
		off_t beg = _s->root.z[_s->nidx];
		uint8_t *m;

		if (UNLIKELY(ftruncate(_s->fd, beg + b.z) < 0)) {
			/* FUCCCK, we might as well hang ourself */
			return -1;
		}
		/* mmap the blob in the file */
		m = mmap_any(_s->fd, PROT_WRITE, MAP_SHARED, beg, b.z);
		if (UNLIKELY(m == MAP_FAILED)) {
			/* this is a fucking disaster */
			return -1;
		}
		/* simple copy it is now, yay */
		memcpy(m, b.data, b.z);
		/* make sure it's on disk, aye */
		(void)msync_any(m, beg, b.z, MS_ASYNC);
		/* write-protect this guy and swap blobs */
		(void)mprot_any(m, beg, b.z, PROT_READ);
		/* unmap the old map */
		munmap(b.data, b.z);
		/* and swap blob data */
		b.data = m;
	}

	/* store in index */
	_s->root.t[_s->nidx + 0U] = b.from;
	/* best effort to guess the next index's timestamp */
	_s->root.t[_s->nidx + 1U] = b.till;
	/* store offsets, cumsum of sizes */
	_s->root.z[_s->nidx + 1U] = _s->root.z[_s->nidx + 0U] + b.z;
	/* store pointer */
	_s->pidx[_s->nidx + 0U] = b.data;
	/* advance number of indices */
	_s->nidx++;
	return 0;
}

static int
_add_idxs(struct _ts_s *_s)
{
	const size_t nidx = _s->nidx;
	off_t beg;
	off_t end;
	uint64_t *m;

	if (_s->fd < 0) {
		return 0;
	}

	/* otherwise the backing file present */
	beg = _ioff(_s);
	end = beg + _ilen(_s);
	if (UNLIKELY(ftruncate(_s->fd, end) < 0)) {
		/* FUCCCK, we might as well hang ourself */
		return -1;
	}
	/* mmap the blob in the file */
	m = mmap_any(_s->fd, PROT_WRITE, MAP_SHARED, beg, end - beg);
	if (UNLIKELY(m == MAP_FAILED)) {
		/* this is a fucking disaster */
		return -1;
	}
	/* bang stamps, then sizes, big-endian */
	for (size_t i = 0U; i <= nidx; i++) {
		m[i] = htobe64(_s->root.t[i]);
	}
	for (size_t i = 0U; i <= nidx; i++) {
		m[nidx + i] = htobe64(_s->root.z[i]);
	}
	/* make sure it's on disk, aye */
	(void)msync_any(m, beg, end - beg, MS_ASYNC);
	/* unmap him right away */
	munmap(m, end - beg);
	return 0;
}

static int
_flush_hdr(const struct _ts_s *_s)
{
	const size_t nflds = _s->public.nfields;

	if (UNLIKELY(_s->fd < 0)) {
		/* no need */
		return 0;
	} else if (UNLIKELY(_s->mdr == NULL)) {
		/* great */
		return 0;
	}
	/* otherwise */
	with (off_t ioff = _ioff(_s)) {
		_s->mdr->ioff = htobe64(_ioff(_s));
		_s->mdr->ooff = htobe64(ioff + _ilen(_s));
	}
	msync_any(_s->mdr, 0U, sizeof(*_s->mdr) + nflds + 1U, MS_ASYNC);
	return 0;
}

static int
_flush(struct _ts_s *_s)
{
/* compact WAL and write contents to backing file */
	const size_t nflds = _s->public.nfields;
	const char *const layo = _s->public.layout;
	const uint64_t *rows = _s->row_scratch;
	struct blob_s b;

	if (UNLIKELY(!_s->nrows)) {
		return 0;
	}
	/* get ourselves a blob first */
	b = _make_blob(layo, nflds, _s->nrows, _s->t, _s->m, rows);

	if (UNLIKELY(b.data == NULL)) {
		/* blimey */
		return -1;
	}

	size_t uncomp = _s->nrows * 2U * sizeof(uint64_t) + 12U * _s->nrows;
	fprintf(stderr, "comp %zu  (%0.2f%%)\n", b.z, 100. * (double)b.z / (double)uncomp);

	/* manifest blob in file */
	_add_blob(_s, b);

	/* write out indices */
	_add_idxs(_s);

	/* update header */
	_flush_hdr(_s);

	_s->nrows = 0U;
	return -1;
}


/* public API */
cots_ts_t
make_cots_ts(const char *layout)
{
	struct _ts_s *res = calloc(1, sizeof(*res));
	size_t laylen;

	if (LIKELY(layout != NULL)) {
		res->public.layout = strdup(layout);
		laylen = strlen(layout);
	} else {
		res->public.layout = nul_layout;
		laylen = 0U;
	}

	/* store layout length as nfields */
	{
		void *nfp = deconst(&res->public.nfields);
		memcpy(nfp, &laylen, sizeof(laylen));

		res->row_scratch = calloc(laylen * NSAMP, sizeof(uint64_t));
	}
	res->obarray = make_cots_ob();

	/* use a backing file? */
	res->fd = -1;

	return (cots_ts_t)res;
}

void
free_cots_ts(cots_ts_t ts)
{
	struct _ts_s *_ts = (void*)ts;

	cots_detach(ts);
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
	struct stat st;
	size_t nflds;
	int fd;

	if ((fd = open(file, flags ? O_RDWR : O_RDONLY)) < 0) {
		return NULL;
	} else if (fstat(fd, &st) < 0) {
		goto clo_out;
	}

	if (UNLIKELY(st.st_size < (ssize_t)sizeof(*res->mdr))) {
		goto clo_out;
	}

	/* read header bit */
	if (read(fd, &hdr, sizeof(hdr)) < (ssize_t)sizeof(hdr)) {
		goto clo_out;
	}
	/* inspect header */
	if (memcmp(hdr.magic, "cots", sizeof(hdr.magic))) {
		/* nope, better fuck off then */
		goto clo_out;
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
		/* determine nflds */
		nflds = eo - layo;
	}

	/* make number of fields known publicly */
	with (void *nfp = deconst(&res->public.nfields)) {
		memcpy(nfp, &nflds, sizeof(nflds));
	}
	/* get some scratch space for this one */
	res->row_scratch = calloc(nflds * NSAMP, sizeof(uint64_t));
	/* map the header for reference */
	with (off_t hz = sizeof(*res->mdr) + nflds + 1U) {
		res->mdr = mmap_any(fd, PROT_READ, MAP_SHARED, 0, hz);
		if (UNLIKELY(res->mdr == NULL)) {
			goto fre_out;
		}
	}

	/* use a backing file */
	return (cots_ts_t)res;

fre_out:
	free(deconst(res->public.filename));
	free(res->row_scratch);
	free(res);
clo_out:
	save_errno {
		close(fd);
	}
	return NULL;
}

int
cots_close_ts(cots_ts_t s)
{
	cots_detach(s);
	free_cots_ts(s);
	return 0;
}


int
cots_attach(cots_ts_t s, const char *file, int flags)
{
/* we've got the following cases
 *    S has data   S has file   FILE has data
 * 1.      -            -             -
 * 2.      -            -             x
 * 3.      -            x             -
 * 4.      -            x             x
 * 5.      x            -             -
 * 6.      x            -             x
 * 7.      x            x             -
 * 8.      x            x             x
 */
	struct fhdr_s *mdr;
	struct stat st;
	int fd;

	if ((fd = open(file, flags, 0666)) < 0) {
		return -1;
	} else if (fstat(fd, &st) < 0) {
		goto clo_out;
	}

	if (LIKELY(!st.st_size)) {
		/* new file, yay, truncate to accomodate header */
		off_t hz = sizeof(*mdr) + s->nfields + 1U;

		if (UNLIKELY(ftruncate(fd, hz) < 0)) {
			goto clo_out;
		}
		/* map the header */
		mdr = mmap_any(fd, PROT_WRITE, MAP_SHARED, 0, hz);
		if (UNLIKELY(mdr == NULL)) {
			goto clo_out;
		}
		/* bang a basic header out */
		with (struct fhdr_s proto = {"cots", "v0", 0x3c3eU}) {
			memcpy(mdr, &proto, sizeof(*mdr));
			memcpy(mdr->layout, s->layout, s->nfields + 1U);
		}
		with (struct _ts_s *_s = (void*)s) {
			/* adjust index offsets, cases 1, 5 */
			if (_s->fd >= 0) {
				/* assume the old attachment has the
				 * fixup already, cases 3, 7 */
				break;
			}
			for (size_t i = 0U; i <= _s->nidx; i++) {
				_s->root.z[i] += hz;
			}
		}

	} else if (st.st_size < (ssize_t)sizeof(*mdr)) {
		/* can't be right */
		goto clo_out;

	} else {
		/* read file's header */
		off_t hz = sizeof(*mdr) + s->nfields + 1U;

		mdr = mmap_any(fd, PROT_READ, MAP_SHARED, 0, hz);
		if (UNLIKELY(mdr == NULL)) {
			goto clo_out;
		}
		/* inspect header, in particular see if fields coincide */
		if (strcmp((const char*)mdr->layout, s->layout)) {
			/* nope, better fuck off then */
			goto unm_out;
		}
		/* yep they do, switch off write protection */
		(void)mprot_any(mdr, 0, hz, PROT_WRITE);
	}

	/* we're good to go, detach any old files */
	cots_detach(s);

	/* attach this one */
	with (struct _ts_s *_s = (void*)s) {
		/* the header as mapped */
		_s->mdr = mdr;
		_s->public.filename = strdup(file);
		_s->fd = fd;
		_s->fl = flags;
	}
	return 0;

unm_out:
	munmap_any(mdr, 0, sizeof(*mdr) + s->nfields + 1U);
clo_out:
	save_errno {
		close(fd);
	}
	return -1;
}

int
cots_detach(cots_ts_t s)
{
	struct _ts_s *_s = (void*)s;

	if (_s->fd >= 0) {
		if (UNLIKELY(_s->nrows)) {
			_flush(_s);
		}
		/* munmap blobs */
		for (size_t i = 0U; i < _s->nidx; i++) {
			if (_s->pidx[i] != NULL) {
				const off_t off = _s->root.z[i];
				const size_t len = _s->root.z[i + 1U] - off;
				munmap_any(_s->pidx[i], off, len);
				_s->pidx[i] = NULL;
			}
		}
		/* reset indices */
		_s->nidx = 0U;
		_s->root.t[0U] = 0U;
		_s->root.z[0U] = 0U;
	}

	if (_s->public.filename) {
		free(deconst(_s->public.filename));
		_s->public.filename = NULL;
	}
	if (_s->mdr) {
		const size_t hz = sizeof(*_s->mdr) + _s->public.nfields + 1U;
		munmap_any(_s->mdr, 0, hz);
		_s->mdr = NULL;
	}
	if (_s->fd >= 0) {
		close(_s->fd);
		_s->fd = -1;
		_s->fl = 0;
	}
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

	_s->t[_s->nrows] = t;
	_s->m[_s->nrows] = m;
	va_start(vap, m);
	for (size_t i = 0U, n = _s->public.nfields,
		     row = _s->nrows * n; i < n; i++) {
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

	if (UNLIKELY(++_s->nrows == NSAMP)) {
		/* auto-eviction */
		_flush(_s);
	}
	return 0;
}

int
cots_write_tick(cots_ts_t s, const struct cots_tick_s *data)
{
	struct _ts_s *_s = (void*)s;
	const size_t nflds = _s->public.nfields;

	_s->t[_s->nrows] = data->toff;
	_s->m[_s->nrows] = data->tag;

	memcpy(&_s->row_scratch[_s->nrows * nflds],
	       data->value, nflds * sizeof(uint64_t));

	if (UNLIKELY(++_s->nrows == NSAMP)) {
		/* auto-eviction */
		_flush(_s);
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
