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

#define NTIDX		(512U)

#define ALGN16(x)	((uintptr_t)((x) + 0xfU) & ~0xfULL)
#define ALGN8(x)	((uintptr_t)((x) + 0x7U) & ~0x7ULL)
#define ALGN4(x)	((uintptr_t)((x) + 0x3U) & ~0x3ULL)
#define ALGN2(x)	((uintptr_t)((x) + 0x1U) & ~0x1ULL)

/* file header, mmapped for convenience */
struct fhdr_s {
	uint8_t magic[4U];
	uint8_t version[2U];
	uint16_t endian;
	/* tba
	 * - lowest 4bits of flags is the log2 of the block size minus 9 */
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

struct pbuf_s {
	/* size of one row with alignment */
	size_t zrow;
	/* tick index */
	size_t rowi;
	/* data */
	uint8_t *data;
};

struct _ts_s {
	struct cots_ts_s public;

	/* mmapped file header */
	struct fhdr_s *mdr;

	/* compacted version of fields */
	char *fields;

	/* obarray for tags */
	cots_ob_t obarray;

	/* row-oriented page buffer */
	struct pbuf_s pb;

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

static size_t
_algn_zrow(const char *layout, size_t nflds)
{
/* calculate sizeof(`layout') with alignments and stuff
 * to be used as partial summator align size of the first NFLDS fields
 * to the alignment requirements of the NFLDS+1st field */
	size_t z = 0U;

	for (size_t i = 0U, inc = sizeof(cots_to_t); i <= nflds; i++) {
		/* add increment from last iteration */
		z += inc;

		switch (layout[i]) {
		case COTS_LO_BYT:
			inc = 1U;
			break;
		case COTS_LO_PRC:
		case COTS_LO_FLT:
			/* round Z up to next 4 multiple */
			z = ALGN4(z);
			inc = 4U;
			break;
		case COTS_LO_TIM:
		case COTS_LO_TAG:
		case COTS_LO_QTY:
		case COTS_LO_DBL:
			/* round Z up to next 8 multiple */
			z = ALGN8(z);
			inc = 8U;
			break;
		case COTS_LO_END:
		default:
			break;
		}
	}
	return z;
}

static struct pbuf_s
_make_pbuf(size_t zrow, size_t blkz)
{
	void *data = calloc(blkz, zrow);
	return (struct pbuf_s){zrow, 0U, data};
}

static struct blob_s
_make_blob(const char *flds, size_t nflds, struct pbuf_s pb)
{
#define Z(zrow, nrows)	(3U * nrows * zrow / 2U)
	struct {
		struct cots_tsoa_s proto;
		void *cols[nflds];
		cots_to_t from;
		cots_to_t till;
	} cols;
	uint8_t *buf;
	size_t nrows;
	size_t bsz;
	size_t z;

	if (UNLIKELY(!(nrows = pb.rowi))) {
		/* trivial */
		return (struct blob_s){0U, NULL};
	}

	/* trial mmap */
	bsz = Z(pb.zrow, pb.rowi);
	buf = mmap(NULL, bsz, PROT_MEM, MAP_MEM, -1, 0);
	if (buf == MAP_FAILED) {
		return (struct blob_s){0U, NULL};
	}

	/* columnarise times */
	cols.proto.toffs = (void*)ALGN16(buf + sizeof(z));
	with (const cots_to_t *t = (const cots_to_t*)pb.data) {
		const size_t acols = pb.zrow / sizeof(*t);

		cols.from = t[0U];
		for (size_t j = 0U; j < nrows; j++) {
			cols.proto.toffs[j] = t[j * acols];
		}
		cols.till = cols.proto.toffs[nrows - 1U];
	}

	/* columnarise */
	for (size_t i = 0U, bi = Z(sizeof(cots_to_t), nrows); i < nflds; i++) {
		/* next one is a bit of a Schlemiel, we could technically
		 * iteratively compute A, much like _algn_zrow() does it,
		 * but this way it saves us some explaining */
		const size_t a = _algn_zrow(flds, i);
		cols.cols[i] = (void*)ALGN16(buf + bi + sizeof(z));

		switch (flds[i]) {
		case COTS_LO_PRC:
		case COTS_LO_FLT: {
			uint32_t *c = cols.cols[i];
			const uint32_t *r = (const uint32_t*)(pb.data + a);
			const size_t acols = pb.zrow / sizeof(*r);

			for (size_t j = 0U; j < nrows; j++) {
				c[j] = r[j * acols];
			}
			bi += Z(sizeof(*c), nrows);
			break;
		}
		case COTS_LO_TIM:
		case COTS_LO_TAG:
		case COTS_LO_QTY:
		case COTS_LO_DBL: {
			uint64_t *c = cols.cols[i];
			const uint64_t *r = (const uint64_t*)(pb.data + a);
			const size_t acols = pb.zrow / sizeof(*r);

			for (size_t j = 0U; j < nrows; j++) {
				c[j] = r[j * acols];
			}
			bi += Z(sizeof(*c), nrows);
			break;
		}
		default:
			break;
		}
	}

	/* call the compactor */
	z = comp(buf + sizeof(z), nflds, nrows, flds, &cols.proto);
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
	return (struct blob_s){z, buf, cols.from, cols.till};

mun_out:
	munmap(buf, bsz);
	return (struct blob_s){0U, NULL};
#undef Z
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
		m[nidx + 1U + i] = htobe64(_s->root.z[i]);
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
	const size_t blkz = _s->public.blockz;

	if (UNLIKELY(_s->fd < 0)) {
		/* no need */
		return 0;
	} else if (UNLIKELY(_s->mdr == NULL)) {
		/* great */
		return 0;
	}
	/* keep track of block size */
	with (unsigned int lgbz = __builtin_ctz(blkz) - 9U) {
		_s->mdr->flags = htobe64(lgbz & 0xfU);
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
	struct blob_s b;

	if (UNLIKELY(!_s->pb.rowi)) {
		return 0;
	}
	/* get ourselves a blob first */
	b = _make_blob(layo, nflds, _s->pb);

	if (UNLIKELY(b.data == NULL)) {
		/* blimey */
		return -1;
	}

	size_t uncomp = _s->pb.rowi * 2U * sizeof(uint64_t) + _s->pb.zrow * _s->pb.rowi;
	fprintf(stderr, "comp %zu  (%0.2f%%)\n", b.z, 100. * (double)b.z / (double)uncomp);

	/* manifest blob in file */
	_add_blob(_s, b);

	/* write out indices */
	_add_idxs(_s);

	/* update header */
	_flush_hdr(_s);

	_s->pb.rowi = 0U;
	return -1;
}

static int
_rd_idx(struct _ts_s *_s)
{
	off_t ioff;
	off_t ooff;
	uint64_t *m;
	size_t nidx;

	if (UNLIKELY(_s->fd < 0)) {
		/* read whence? */
		return -1;
	}

	/* poke the header to tell us them numbers */
	ioff = be64toh(_s->mdr->ioff);
	ooff = be64toh(_s->mdr->ooff);

	/* map that */
	m = mmap_any(_s->fd, PROT_READ, MAP_PRIVATE, ioff, ooff - ioff);
	if (UNLIKELY(m == NULL)) {
		return -1;
	}
	/* determine nidx */
	nidx = (ooff - ioff) / (sizeof(*_s->root.t) + sizeof(*_s->root.z)) - 1U;

	/* copy stuff to our index structure */
	for (size_t i = 0U; i <= nidx; i++) {
		_s->root.t[i] = be64toh(m[i]);
	}
	for (size_t i = 0U; i <= nidx; i++) {
		_s->root.z[i] = be64toh(m[nidx + 1U + i]);
	}
	/* good effort */
	munmap_any(m, ioff, ooff - ioff);
	/* lest we forget about nidx */
	_s->nidx = nidx;
	return 0;
}


/* public API */
cots_ts_t
make_cots_ts(const char *layout, size_t blockz)
{
	struct _ts_s *res = calloc(1, sizeof(*res));
	size_t laylen;
	size_t zrow;

	if (LIKELY(layout != NULL)) {
		res->public.layout = strdup(layout);
		laylen = strlen(layout);
		zrow = _algn_zrow(layout, laylen);
	} else {
		res->public.layout = nul_layout;
		laylen = 0U;
		zrow = 0U;
	}

	/* use default block size? */
	blockz = blockz ?: 8192U;

	/* store layout length as nfields and blocksize as blockz */
	{
		void *nfp = deconst(&res->public.nfields);
		void *bzp = deconst(&res->public.blockz);

		memcpy(nfp, &laylen, sizeof(laylen));
		memcpy(bzp, &blockz, sizeof(blockz));
	}

	res->obarray = make_cots_ob();

	/* make a page buffer (WAL) */
	res->pb = _make_pbuf(zrow, blockz);

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
	if (LIKELY(_ts->pb.data != NULL)) {
		free(_ts->pb.data);
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
	size_t blkz;
	size_t zrow;
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
	/* read block size */
	with (uint64_t fl = be64toh(hdr.flags)) {
		blkz = 1UL << ((fl & 0xfU) + 9U);
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
		/* determine aligned size */
		zrow = _algn_zrow(layo, nflds);
	}

	/* make number of fields known publicly */
	with (void *nfp = deconst(&res->public.nfields)) {
		memcpy(nfp, &nflds, sizeof(nflds));
	}
	/* make blocksize known publicly */
	with (void *bzp = deconst(&res->public.blockz)) {
		memcpy(bzp, &blkz, sizeof(blkz));
	}

	/* get some scratch space for this one */
	res->pb = _make_pbuf(zrow, blkz);
	/* map the header for reference */
	with (off_t hz = sizeof(*res->mdr) + nflds + 1U) {
		res->mdr = mmap_any(fd, PROT_READ, MAP_SHARED, 0, hz);
		if (UNLIKELY(res->mdr == NULL)) {
			goto fre_out;
		}
	}
	/* now read them indices */
	_rd_idx(res);

	/* use a backing file */
	return (cots_ts_t)res;

fre_out:
	free(deconst(res->public.filename));
	free(res->pb.data);
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
		if (UNLIKELY(_s->pb.rowi)) {
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
cots_write_va(cots_ts_t s, cots_to_t t, ...)
{
	struct _ts_s *_s = (void*)s;
	const char *flds = _s->public.layout;
	uint8_t *rp = _s->pb.data + _s->pb.rowi * _s->pb.zrow;
	va_list vap;

	with (cots_to_t *tp = (void*)rp) {
		*tp = t;
	}
	va_start(vap, t);
	for (size_t i = 0U, n = _s->public.nfields; i < n; i++) {
		/* next one is a bit of a Schlemiel, we could technically
		 * iteratively compute A, much like _algn_zrow() does it,
		 * but this way it saves us some explaining */
		const size_t a = _algn_zrow(flds, i);

		switch (_s->public.layout[i]) {
		case COTS_LO_PRC:
		case COTS_LO_FLT: {
			uint32_t *cp = (uint32_t*)(rp + a);
			*cp = va_arg(vap, uint32_t);
			break;
		}
		case COTS_LO_TIM:
		case COTS_LO_TAG:
		case COTS_LO_QTY:
		case COTS_LO_DBL: {
			uint64_t *cp = (uint64_t*)(rp + a);
			*cp = va_arg(vap, uint64_t);
			break;
		}
		default:
			break;
		}
	}
	va_end(vap);

	if (UNLIKELY(++_s->pb.rowi == _s->public.blockz)) {
		/* auto-eviction */
		_flush(_s);
	}
	return 0;
}

int
cots_write_tick(cots_ts_t s, const struct cots_tick_s *data)
{
	struct _ts_s *_s = (void*)s;
	const size_t blkz = _s->public.blockz;

	memcpy(&_s->pb.data[_s->pb.rowi * _s->pb.zrow], data, _s->pb.zrow);

	if (UNLIKELY(++_s->pb.rowi == blkz)) {
		/* auto-eviction */
		_flush(_s);
	}
	return 0;
}

ssize_t
cots_read_ticks(struct cots_tsoa_s *tsoa, cots_ts_t s)
{
	struct _ts_s *_s = (void*)s;
	const size_t nflds = _s->public.nfields;
	const size_t blkz = _s->public.blockz;
	const char *layo = _s->public.layout;
	size_t n = 0U;
	size_t z;
	off_t poff;
	off_t eoff;
	uint8_t *m;
	size_t mi = 0U;

	/* we need a cursor type! */
	;

	/* find offset of page and length */
	poff = _s->root.z[0U];
	eoff = _s->root.z[1U];

	if (UNLIKELY(eoff <= poff)) {
		return -1;
	}
	/* otherwise map */
	m = mmap_any(_s->fd, PROT_READ, MAP_SHARED, poff, eoff - poff);
	if (UNLIKELY(m == NULL)) {
		return -1;
	}

	/* quickly inspect integrity */
	memcpy(&z, m, sizeof(z));
	mi += sizeof(z);
	if (UNLIKELY(z != eoff - poff - sizeof(z))) {
		goto mun_out;
	}

	/* setup result soa */
	tsoa->toffs = (cots_to_t*)_s->pb.data;
	for (size_t i = 0U; i < nflds; i++) {
		tsoa->cols[i] = tsoa->toffs + (i + 1U) * blkz;
	}

	/* decompress */
	n = dcmp(tsoa, nflds, layo, m + mi, z);

mun_out:
	munmap_any(m, poff, z);
	return n;
}

/* cotse.c ends here */
