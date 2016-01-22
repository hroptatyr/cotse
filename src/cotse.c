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
#if defined HAVE_SYS_SENDFILE_H
# include <sys/sendfile.h>
#endif	/* HAVE_SYS_SENDFILE_H */
#include <fcntl.h>
#include <errno.h>
#include "cotse.h"
#include "index.h"
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
	/* offset to meta */
	uint64_t moff;
	/* offset to next series (presumably the index series) */
	uint64_t noff;
	/* layout, \nul term'd */
	uint8_t layout[];
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

struct _ss_s {
	struct cots_ss_s public;

	/* mmapped file header */
	struct fhdr_s *mdr;

	/* compacted version of fields */
	char *fields;

	/* flags for bookkeeping and stuff */
	struct {
		unsigned int frozen:1;
	};

	/* row-oriented page buffer */
	struct pbuf_s pb;

	/* currently attached file and its opening flags */
	int fd;
	int fl;
	/* current offset for next blob */
	off_t fo;
	/* current offset for reading */
	off_t ro;

	/* index, if any, this will be recursive */
	cots_idx_t idx;
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


static inline __attribute__((pure, const)) size_t
min_z(size_t x, size_t y)
{
	return x < y ? x : y;
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
		case COTS_LO_CNT:
		case COTS_LO_TAG:
		case COTS_LO_SIZ:
		case COTS_LO_QTY:
		case COTS_LO_DBL:
			/* round Z up to next 8 multiple */
			z = ALGN8(z);
			inc = 8U;
			break;
		case COTS_LO_END:
		default:
			z = ALGN8(z);
			inc = 0U;
			break;
		}
	}
	return z;
}

static inline struct pbuf_s
_make_pbuf(size_t zrow, size_t blkz)
{
	void *data = calloc(blkz, zrow);
	return (struct pbuf_s){zrow, 0U, data};
}

static inline void
_free_pbuf(struct pbuf_s pb)
{
	if (LIKELY(pb.data != NULL)) {
		free(pb.data);
	}
	return;
}

static struct blob_s
_make_blob(const char *flds, size_t nflds, struct pbuf_s pb)
{
#define Z(zrow, nrows)	(3U * (nrows) * (zrow) / 2U)
	struct {
		struct cots_tsoa_s proto;
		void *cols[nflds];
		cots_to_t from;
		cots_to_t till;
	} cols;
	uint8_t *buf;
	size_t nrows;
	size_t bi;
	size_t bsz;
	size_t z;

	if (UNLIKELY(!(nrows = pb.rowi))) {
		/* trivial */
		return (struct blob_s){0U, NULL};
	}

	/* trial mmap */
	bi = Z(pb.zrow, nrows > 64U ? nrows : 64U);
	bsz = 2U * bi;
	buf = mmap(NULL, bsz, PROT_MEM, MAP_MEM, -1, 0);
	if (buf == MAP_FAILED) {
		return (struct blob_s){0U, NULL};
	}

	/* columnarise times */
	cols.proto.toffs = (void*)ALGN16(buf + bi + sizeof(z));
	with (const cots_to_t *t = (const cots_to_t*)pb.data) {
		const size_t acols = pb.zrow / sizeof(*t);

		cols.from = t[0U];
		for (size_t j = 0U; j < nrows; j++) {
			cols.proto.toffs[j] = t[j * acols];
		}
		cols.till = cols.proto.toffs[nrows - 1U];
		bi += Z(sizeof(cots_to_t), nrows);
	}

	/* columnarise */
	for (size_t i = 0U; i < nflds; i++) {
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
		case COTS_LO_CNT:
		case COTS_LO_TAG:
		case COTS_LO_SIZ:
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

static void
_free_blob(struct blob_s b)
{
	munmap_any(b.data, 0U, b.z);
	return;
}

static inline __attribute__((const)) size_t
_hdrz(const struct _ss_s *_s)
{
	const size_t nflds = _s->public.nfields;
	return sizeof(*_s->mdr) + nflds + 1U;
}


/* file fiddling */
static int
_updt_hdr(const struct _ss_s *_s, size_t metaz)
{
	if (UNLIKELY(_s->mdr == NULL)) {
		/* great */
		return 0;
	}
	_s->mdr->moff = htobe64(_s->fo);
	_s->mdr->noff = htobe64(_s->fo + metaz);
	msync_any(_s->mdr, 0U, _hdrz(_s), MS_ASYNC);
	return 0;
}

static int
_flush(struct _ss_s *_s)
{
/* compact WAL and write contents to backing file */
	const size_t nflds = _s->public.nfields;
	const char *const layo = _s->public.layout;
	struct blob_s b;
	size_t metaz = 0U;
	int rc = 0;

	if (UNLIKELY(!_s->pb.rowi)) {
		return 0;
	} else if (UNLIKELY(_s->fd < 0)) {
		rc = -1;
		goto rst_out;
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
	(void)lseek(_s->fd, _s->fo, SEEK_SET);
	/* simply write stuff */
	for (ssize_t nwr, rem = b.z; rem > 0; rem -= nwr) {
		nwr = write(_s->fd, b.data + (b.z - rem), rem);

		if (UNLIKELY(nwr < 0)) {
			/* truncate back to old size */
			(void)ftruncate(_s->fd, _s->fo);
			rc = -1;
			goto fre_out;
		}
	}
	/* add to index */
	if (_s->idx) {
		cots_add_index(
			_s->idx,
			(struct trng_s){b.from, b.till},
			(struct orng_s){_s->fo, _s->fo + b.z},
			_s->pb.rowi);
	}

	/* advance file offset and celebrate */
	_s->fo += b.z;

	/* put stuff like field names, obarray, etc. into the meta section
	 * this will not update the FO */
	if (_s->fields) {
		const char *_1st = _s->public.fields[0U];
		const char *last = _s->public.fields[nflds - 1U];

		metaz = last - _1st + strlen(last) + 1U;
	}
	/* manifest in file */
	if (metaz) {
		ssize_t nwr = write(_s->fd, (_s->fields ?: nul_layout), metaz);

		if (UNLIKELY(nwr < 0)) {
			/* truncate back to old size */
			(void)ftruncate(_s->fd, _s->fo);
			rc = -1;
			goto fre_out;
		}
	}

	/* update header */
	_updt_hdr(_s, metaz);

fre_out:
	_free_blob(b);
rst_out:
	_s->pb.rowi = 0U;
	return rc;
}

static ssize_t
_cat(struct _ss_s *restrict _s, const struct cots_ss_s *src)
{
/* sendfile(3) disk data from SRC to _S */
	const struct _ss_s *_src = (const void*)src;
	struct stat st;
	ssize_t fz;
	off_t so = 0;

	if (UNLIKELY(_src->fd < 0)) {
		/* bollocks */
		return -1;
	} else if (UNLIKELY(fstat(_s->fd, &st) < 0)) {
		/* interesting */
		return -1;
	} else if (UNLIKELY((fz = st.st_size) < 0)) {
		/* right, what are we, a freak show? */
		return -1;
	} else if (UNLIKELY(fstat(_src->fd, &st) < 0)) {
		/* no work today? i'm going home */
		return -1;
	}

	while (so < st.st_size) {
		ssize_t nsf = sendfile(_s->fd, _src->fd, &so, st.st_size - so);

		if (UNLIKELY(nsf <= 0)) {
			goto tru_out;
		}
	}
	return so;

tru_out:
	/* we can't do with failures, better reset this thing */
	(void)ftruncate(_s->fd, fz);
	return -1;
}


/* public series storage API */
cots_ss_t
make_cots_ss(const char *layout, size_t blockz)
{
	struct _ss_s *res;
	size_t laylen;
	size_t zrow;

	if (UNLIKELY((res = calloc(1U, sizeof(*res))) == NULL)) {
		/* nothing we can do, is there */
		return NULL;
	}

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

	/* make a page buffer (WAL) */
	res->pb = _make_pbuf(zrow, blockz);

	/* use a backing file? */
	res->fd = -1;

	return (cots_ss_t)res;
}

void
free_cots_ss(cots_ss_t ss)
{
	struct _ss_s *_ss = (void*)ss;

	cots_detach(ss);
	if (LIKELY(_ss->public.layout != nul_layout)) {
		free(deconst(_ss->public.layout));
	}
	if (_ss->public.fields != NULL) {
		free(deconst(_ss->public.fields));
		free(_ss->fields);
	}
	_free_pbuf(_ss->pb);
	free(_ss);
	return;
}

cots_ss_t
cots_open_ss(const char *file, int flags)
{
	struct _ss_s *res;
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
	if (flags) {
		res->pb = _make_pbuf(zrow, blkz);
	}
	/* map the header for reference */
	res->mdr = mmap_any(fd, PROT_READ, MAP_SHARED, 0, _hdrz(res));
	if (UNLIKELY(res->mdr == NULL)) {
		goto fre_out;
	}

	/* collect details about this backing file */
	res->fd = fd;
	res->fl = flags;
	res->fo = be64toh(res->mdr->moff) ?: st.st_size;
	res->ro = _hdrz(res);

	/* use a backing file */
	return (cots_ss_t)res;

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
cots_close_ss(cots_ss_t s)
{
	cots_detach(s);
	free_cots_ss(s);
	return 0;
}


int
cots_attach(cots_ss_t s, const char *file, int flags)
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
		off_t hz = _hdrz((struct _ss_s*)s);
		/* calculate blocksize for header */
		unsigned int lgbz = __builtin_ctz(s->blockz) - 9U;

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
			/* keep track of block size */
			proto.flags = htobe64(lgbz & 0xfU);

			memcpy(mdr, &proto, sizeof(*mdr));
			memcpy(mdr->layout, s->layout, s->nfields + 1U);
		}
		/* fiddle with the stat */
		st.st_size = hz;

	} else if (st.st_size < (ssize_t)sizeof(*mdr)) {
		/* can't be right */
		goto clo_out;

	} else {
		/* read file's header */
		const off_t hz = _hdrz((struct _ss_s*)s);

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
	with (struct _ss_s *_s = (void*)s) {
		/* the header as mapped */
		_s->mdr = mdr;
		_s->public.filename = strdup(file);
		_s->fd = fd;
		_s->fl = flags;
		/* store current index offs or file size as blob offs */
		_s->fo = be64toh(mdr->moff) ?: st.st_size;
		_s->ro = _hdrz(_s);
	}
	return 0;

unm_out:
	munmap_any(mdr, 0, _hdrz((struct _ss_s*)s));
clo_out:
	save_errno {
		close(fd);
	}
	return -1;
}

int
cots_detach(cots_ss_t s)
{
/* this is the authority in closing,
 * both free_cots_ss() and cots_close_ss() will unconditionally call this. */
	struct _ss_s *_s = (void*)s;

	cots_freeze(s);

	if (_s->idx) {
		/* assume index has been dealt with in _freeze() */
		free_cots_idx(_s->idx);
	}
	if (_s->public.filename) {
		free(deconst(_s->public.filename));
		_s->public.filename = NULL;
	}
	if (_s->mdr) {
		munmap_any(_s->mdr, 0, _hdrz(_s));
		_s->mdr = NULL;
	}
	if (_s->fd >= 0) {
		close(_s->fd);
		_s->fd = -1;
		_s->fl = 0;
		_s->fo = 0;
		_s->ro = 0;
	}
	return 0;
}

int
cots_freeze(cots_ss_t s)
{
	struct _ss_s *_s = (void*)s;
	int rc;

	if (UNLIKELY(_s->fd < 0)) {
		/* not on my watch */
		return -1;
	} else if (_s->frozen) {
		return 0;
	}

	/* flush wal to file */
	rc = _flush(_s);
	/* set frozen flag */
	_s->frozen = 1U;

	if (_s->idx) {
		/* this is bad coupling:
		 * we know _s->idx is in fact a normal _ss_s object
		 * just freeze things here,
		 * then use its fd and sendfile(3) to append index */
		cots_freeze(_s->idx);
		_cat(_s, _s->idx);
	}
	return rc;
}


int
cots_bang_tick(cots_ss_t s, const struct cots_tick_s *data)
{
	struct _ss_s *_s = (void*)s;

	memcpy(&_s->pb.data[_s->pb.rowi * _s->pb.zrow], data, _s->pb.zrow);
	return 0;
}

int
cots_keep_last(cots_ss_t s)
{
	struct _ss_s *_s = (void*)s;
	const size_t blkz = _s->public.blockz;
	int rc = 0;

	if (UNLIKELY(++_s->pb.rowi == blkz)) {
		/* just for now */
		if (!_s->idx) {
			_s->idx = make_cots_idx(_s->public.filename);
		}
		/* auto-eviction */
		rc = _flush(_s);
	}
	return rc;
}


int
cots_write_tick(cots_ss_t s, const struct cots_tick_s *data)
{
	return !(cots_bang_tick(s, data) < 0)
		? cots_keep_last(s)
		: -1;
}

int
cots_write_va(cots_ss_t s, cots_to_t t, ...)
{
	struct _ss_s *_s = (void*)s;
	const char *flds = _s->public.layout;
	uint8_t rp[_s->pb.zrow] __attribute__((aligned(16)));
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
		case COTS_LO_CNT:
		case COTS_LO_TAG:
		case COTS_LO_SIZ:
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

	return cots_write_tick(s, (const void*)rp);
}

int
cots_init_tsoa(struct cots_tsoa_s *restrict tgt, cots_ss_t s)
{
	const size_t blkz = s->blockz;
	const size_t nflds = s->nfields;
	struct pbuf_s rb = _make_pbuf(sizeof(uint64_t) * (nflds + 1U), blkz);

	if (UNLIKELY((tgt->toffs = (cots_to_t*)rb.data) == NULL)) {
		return -1;
	}
	for (size_t i = 0U; i < nflds; i++) {
		tgt->cols[i] = tgt->toffs + (i + 1U) * blkz;
	}
	return 0;
}

int
cots_fini_tsoa(struct cots_tsoa_s *restrict tgt, cots_ss_t UNUSED(s))
{
	if (UNLIKELY(tgt->toffs == NULL)) {
		return -1;
	}
	free(tgt->toffs);
	return 0;
}

ssize_t
cots_read_ticks(struct cots_tsoa_s *restrict tgt, cots_ss_t s)
{
/* currently this is mmap only */
	struct _ss_s *_s = (void*)s;
	const size_t blkz = _s->public.blockz;
	const size_t nflds = _s->public.nfields;
	const char *layo = _s->public.layout;
	size_t nt = 0U;
	size_t mz;
	uint8_t *mp;
	size_t rz;

	if (UNLIKELY(_s->ro >= _s->fo)) {
		/* no recently added ticks, innit? */
		return 0;
	} else if (UNLIKELY(_s->fd < 0)) {
		/* not backing file */
		return -1;
	}

	/* guesstimate the page that needs mapping */
	mz = min_z(_s->fo - _s->ro, blkz * nflds * sizeof(uint64_t));
	mp = mmap_any(_s->fd, PROT_READ, MAP_SHARED, _s->ro, mz);
	if (UNLIKELY(mp == NULL)) {
		return -1;
	}

	/* quickly inspect integrity, well update RO more importantly */
	memcpy(&rz, mp, sizeof(rz));

	/* decompress */
	nt = dcmp(tgt, nflds, layo, mp + sizeof(rz), rz);

	/* unmap */
	munmap_any(mp, _s->ro, mz);

	/* advance iterator */
	_s->ro += rz + sizeof(rz);
	return nt;
}


/* meta stuff */
int
cots_put_fields(cots_ss_t s, const char **fields)
{
	struct _ss_s *_s = (void*)s;
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

/* cotse.c ends here */
