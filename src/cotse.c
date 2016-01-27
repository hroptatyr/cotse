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
#include "wal.h"
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
	/* should be "cots" */
	uint8_t magic[4U];
	/* should be "v0" */
	uint8_t version[2U];
	/* COTS_ENDIAN written in native endian */
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

/* meta chunks */
struct chnk_s {
	const uint8_t *data;
	size_t z;
	uint8_t type;
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

	/* row-oriented page buffer, wal */
	struct cots_wal_s *wal;
	/* memory wal for swapsies */
	struct cots_wal_s *mwal;

	/* currently attached file and its opening flags */
	int fd;
	int fl;
	/* current offset for next blob */
	off_t fo;
	/* current offset for reading */
	off_t ro;

	/* index, if any, this will be recursive */
	cots_idx_t idx;

	/* obarray */
	cots_ob_t ob;
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

static inline __attribute__((const, pure)) size_t
exp_lgbz(uint64_t b)
{
	return 1ULL << (b + 9U);
}

static inline __attribute__((const, pure)) uint64_t
log_blkz(size_t b)
{
	return __builtin_ctz(b) - 9U;
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
		case COTS_LO_STR:
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


static struct blob_s
_make_blob(const char *flds, size_t nflds, const struct cots_wal_s *w)
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

	if (UNLIKELY(!(nrows = _wal_rowi(w)))) {
		/* trivial */
		return (struct blob_s){0U, NULL};
	}

	/* trial mmap */
	bi = Z(w->zrow, nrows > 64U ? nrows : 64U);
	bsz = 2U * bi;
	buf = mmap(NULL, bsz, PROT_MEM, MAP_MEM, -1, 0);
	if (buf == MAP_FAILED) {
		return (struct blob_s){0U, NULL};
	}

	/* columnarise times */
	cols.proto.toffs = (void*)ALGN16(buf + bi + sizeof(z));
	with (const cots_to_t *t = (const cots_to_t*)w->data) {
		const size_t acols = w->zrow / sizeof(*t);

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
			const uint32_t *r = (const uint32_t*)(w->data + a);
			const size_t acols = w->zrow / sizeof(*r);

			for (size_t j = 0U; j < nrows; j++) {
				c[j] = r[j * acols];
			}
			bi += Z(sizeof(*c), nrows);
			break;
		}
		case COTS_LO_TIM:
		case COTS_LO_CNT:
		case COTS_LO_STR:
		case COTS_LO_SIZ:
		case COTS_LO_QTY:
		case COTS_LO_DBL: {
			uint64_t *c = cols.cols[i];
			const uint64_t *r = (const uint64_t*)(w->data + a);
			const size_t acols = w->zrow / sizeof(*r);

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
	/* store compacted size and number of rows
	 * seeing as the maximum blocksize can be 2^24 and storing 0 rows
	 * would not be beneficial we store nrows-1 in the first 24bits
	 * and then the compressed size */
	with (uint64_t zn = (z << 24U) ^ (nrows - 1U)) {
		zn = htobe64(zn);
		memcpy(buf, &zn, sizeof(zn));
		z += sizeof(zn);
	}
	/* to traverse the file backwards, store the size and a crc24 */
	with (uint64_t zc = (z << 24U) ^ (0U)) {
		zc = htobe64(zc);
		memcpy(buf + z, &zc, sizeof(zc));
		z += sizeof(zc);
	}

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

static inline __attribute__((const)) cots_to_t
_last_toff(const struct _ss_s *_s)
{
/* obtain the time-offset of the last kept tick */
	const cots_to_t *const tp = (void*)_s->wal->data;
	const size_t zrow = _s->wal->zrow;
	const size_t rowi = _wal_rowi(_s->wal);

	/* assume the bang thing duped the last tick toff so
	 * it's at position rowi */
	return tp != NULL ? tp[rowi * zrow / sizeof(cots_to_t)] : -1ULL;
}

static ssize_t
_wr_meta_chnk(int fd, struct chnk_s chnk)
{
/* write a chunk of meta data */
	uint64_t tz = (chnk.z << 8U) ^ (chnk.type);
	ssize_t nwr = 0;

	/* big-endianify */
	tz = htobe64(tz);
	/* write tz, then data */
	nwr += write(fd, &tz, sizeof(tz));
	nwr += write(fd, chnk.data, chnk.z);
	return (nwr == chnk.z + sizeof(tz)) ? nwr : -1;
}

static struct chnk_s
_rd_meta_chnk(const uint8_t *chnk, size_t chnz)
{
	uint64_t tz;
	size_t z;
	uint8_t t;

	if (UNLIKELY(chnz < sizeof(tz))) {
		/* can't be */
		return (struct chnk_s){NULL};
	}
	/* otherwise snarf the type-size */
	memcpy(&tz, chnk, sizeof(tz));
	tz = be64toh(tz);
	t = tz & 0xffU;
	z = tz >> 8U;
	/* check sizes again, avoid overreading a page boundary */
	if (UNLIKELY(sizeof(tz) + z > chnz)) {
		return (struct chnk_s){NULL};
	}
	/* otherwise it's good to go */
	return (struct chnk_s){chnk + sizeof(tz), z, t};
}


/* file fiddling */
static int _bang_fields(struct _ss_s *_s, const char *flds, size_t fldz);

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

static size_t
_wr_meta(const struct _ss_s *_s)
{
	size_t res = 0U;

	/* deal with them fields first */
	if (_s->fields) {
		const size_t nflds = _s->public.nfields;
		const char *_1st = _s->public.fields[0U];
		const char *last = _s->public.fields[nflds - 1U];
		ssize_t mz = last - _1st + strlen(last) + 1U;
		ssize_t nwr;

		nwr = _wr_meta_chnk(
			_s->fd, (struct chnk_s){(uint8_t*)_s->fields, mz, 'F'});

		if (UNLIKELY(nwr < 0)) {
			/* truncate back to old size */
			goto tru_out;
		}
		res += nwr;
	}

	if (_s->ob != NULL) {
		const uint8_t *tgt;
		size_t mz = wr_ob(&tgt, _s->ob);
		ssize_t nwr;

		nwr = _wr_meta_chnk(_s->fd, (struct chnk_s){tgt, mz, 'O'});

		if (UNLIKELY(nwr < 0)) {
			/* truncate back to old size */
			goto tru_out;
		}
		res += nwr;
	}
	return res;

tru_out:
	(void)ftruncate(_s->fd, _s->fo);
	return 0U;
}

static int
_rd_meta(struct _ss_s *restrict _s, const uint8_t *blob, size_t bloz)
{
	struct chnk_s c;

	for (size_t bi = 0U;
	     bi < bloz && (c = _rd_meta_chnk(blob + bi, bloz - bi)).data;
	     bi = c.data + c.z - blob) {
		/* only handle chunks with known types */
		switch (c.type) {
		case 'F':
			/* fields, yay */
			_bang_fields(_s, (const char*)c.data, c.z);
			break;

		case 'O':
			/* obarray, fantastic */
			with (cots_ob_t nuob = rd_ob(c.data, c.z)) {
				if (nuob != NULL && _s->ob != NULL) {
					free_cots_ob(_s->ob);
				}
				_s->ob = nuob;
			}
			break;

		default:
			/* user rubbish */
			break;
		}
	}
	return (c.data != NULL) - 1;
}

static int
_flush(struct _ss_s *_s)
{
/* compact WAL and write contents to backing file */
	const size_t nflds = _s->public.nfields;
	const char *const layo = _s->public.layout;
	size_t rowi;
	struct blob_s b;
	size_t mz;
	int rc = 0;

	if (UNLIKELY(_s->wal == NULL)) {
		return 0;
	} else if (UNLIKELY(!(rowi = _wal_rowi(_s->wal)))) {
		return 0;
	} else if (UNLIKELY(_s->fd < 0)) {
		rc = -1;
		goto rst_out;
	}

	/* get ourselves a blob first */
	b = _make_blob(layo, nflds, _s->wal);

	if (UNLIKELY(b.data == NULL)) {
		/* blimey */
		return -1;
	}

	with (size_t uncomp = _s->wal->zrow * rowi) {
		fprintf(stderr, "comp %zu  (%0.2f%%)\n",
			b.z, 100. * (double)b.z / (double)uncomp);
	}

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
	/* advance file offset and celebrate */
	_s->fo += b.z;

	/* add to index */
	if (_s->idx) {
		cots_add_index(
			_s->idx,
			(struct trng_s){b.from, b.till},
			(struct orng_s){_s->fo - b.z, _s->fo},
			rowi);
	}

	/* put stuff like field names, obarray, etc. into the meta section
	 * this will not update the FO */
	mz = _wr_meta(_s);

	/* update header */
	_updt_hdr(_s, mz);

fre_out:
	_free_blob(b);
rst_out:
	_wal_rset(_s->wal);
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
cots_ts_t
make_cots_ts(const char *layout, size_t blockz)
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
	res->wal = _make_wal(zrow, blockz);

	/* use a backing file? */
	res->fd = -1;

	/* create an obarray if there's strings */
	if (strchr(res->public.layout, COTS_LO_STR)) {
		res->ob = make_cots_ob();
	}
	return (cots_ts_t)res;
}

void
free_cots_ts(cots_ts_t s)
{
	struct _ss_s *_s = (void*)s;

	cots_detach(s);
	if (LIKELY(_s->public.layout != nul_layout)) {
		free(deconst(_s->public.layout));
	}
	if (_s->public.fields != NULL) {
		free(deconst(_s->public.fields));
		free(_s->fields);
	}
	if (LIKELY(_s->wal != NULL)) {
		_free_wal(_s->wal);
	}
	if (UNLIKELY(_s->mwal != NULL)) {
		_free_wal(_s->mwal);
	}
	if (_s->ob != NULL) {
		free_cots_ob(_s->ob);
	}
	free(_s);
	return;
}

cots_ts_t
cots_open_ts(const char *file, int flags)
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
		blkz = exp_lgbz(fl & 0xfU);
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

	/* short dip into the meta pool */
	with (off_t noff = be64toh(res->mdr->noff)) {
		off_t moff = res->fo;
		uint8_t *m;

		if (moff >= noff) {
			/* not for us this isn't */
			break;
		}
		/* try mapping him */
		m = mmap_any(fd, PROT_READ, MAP_SHARED, moff, noff - moff);
		if (UNLIKELY(m == NULL)) {
			/* it's no good */
			break;
		}
		/* try reading him */
		(void)_rd_meta(res, m, noff - moff);

		(void)munmap_any(m, moff, noff - moff);
	}

	/* get some scratch space and dissect the file parts */
	if (flags/*O_RDWR*/) {
		off_t so = be64toh(res->mdr->noff) ?: st.st_size;
		size_t flen = strlen(file);
		/* for the temp file name */
		char idxfn[flen + 5U];
		int ifd;

		/* get breathing space */
		if (UNLIKELY((res->wal = _make_wal(zrow, blkz)) == NULL)) {
			goto fre_out;
		}

		/* construct temp filename */
		memcpy(idxfn, file, flen);
		memcpy(idxfn + flen, ".idx", sizeof(".idx"));

		/* check if exists first? */
		ifd = open(idxfn, O_CREAT | O_TRUNC | O_RDWR, 0666);

		/* evacuate index */
		while (so < st.st_size) {
			ssize_t nsf;

			nsf = sendfile(ifd, res->fd, &so, st.st_size - so);
			if (UNLIKELY(nsf <= 0)) {
				/* oh great :| */
				unlink(idxfn);
			}
		}
		close(ifd);

		/* truncate to size without index nor meta */
		(void)ftruncate(res->fd, res->fo);
	}

	/* use a backing file */
	return (cots_ts_t)res;

fre_out:
	free(deconst(res->public.filename));
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
		/* new file, yay, truncate to accomodate header and WAL */
		const off_t hz = _hdrz((struct _ss_s*)s);

		if (UNLIKELY(ftruncate(fd, hz) < 0)) {
			goto clo_out;
		}
		/* map the header */
		mdr = mmap_any(fd, PROT_READ | PROT_WRITE, MAP_SHARED, 0, hz);
		if (UNLIKELY(mdr == NULL)) {
			goto clo_out;
		}
		/* bang a basic header out */
		with (struct fhdr_s proto = {"cots", "v0", COTS_ENDIAN}) {
			/* calculate blocksize for header */
			unsigned int lgbz = log_blkz(s->blockz);

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

		/* (re)attach the wal */
		_s->mwal = _s->wal;
		_s->wal = _wal_attach(_s->mwal, file);
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
cots_detach(cots_ts_t s)
{
/* this is the authority in closing,
 * both free_cots_ts() and cots_close_ss() will unconditionally call this. */
	struct _ss_s *_s = (void*)s;

	cots_freeze(s);

	if (_s->wal && _wal_detach(_s->wal, _s->public.filename) < 0) {
		/* great, just keep using the wal */
		;
	} else if (_s->wal) {
		/* swap with spare wal */
		_s->wal = _s->mwal;
		_s->mwal = NULL;
	}
	if (_s->idx) {
		/* assume index has been dealt with in _freeze() */
		free_cots_idx(_s->idx);
		_s->idx = NULL;
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
cots_freeze(cots_ts_t s)
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
cots_bang_tick(cots_ts_t s, const struct cots_tick_s *data)
{
	struct _ss_s *_s = (void*)s;

	if (UNLIKELY(data->toff < _last_toff(_s))) {
		/* can't go back in time */
		return -1;
	}
	/* otherwise bang, this is a wal routine */
	_wal_bang(_s->wal, data);
	return 0;
}

int
cots_keep_last(cots_ts_t s)
{
	struct _ss_s *_s = (void*)s;
	const size_t blkz = _s->public.blockz;
	int rc = 0;

	if (UNLIKELY(_wal_rinc(_s->wal) == blkz)) {
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
cots_write_tick(cots_ts_t s, const struct cots_tick_s *data)
{
	return !(cots_bang_tick(s, data) < 0)
		? cots_keep_last(s)
		: -1;
}

int
cots_write_va(cots_ts_t s, cots_to_t t, ...)
{
	struct _ss_s *_s = (void*)s;
	const char *flds = _s->public.layout;
	uint8_t rp[_s->wal->zrow] __attribute__((aligned(16)));
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
		case COTS_LO_STR:
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
cots_init_tsoa(struct cots_tsoa_s *restrict tgt, cots_ts_t s)
{
	const size_t blkz = s->blockz;
	const size_t nflds = s->nfields;
	void *rb = calloc(sizeof(uint64_t) * (nflds + 1U), blkz);

	if (UNLIKELY((tgt->toffs = (cots_to_t*)rb) == NULL)) {
		return -1;
	}
	for (size_t i = 0U; i < nflds; i++) {
		tgt->cols[i] = tgt->toffs + (i + 1U) * blkz;
	}
	return 0;
}

int
cots_fini_tsoa(struct cots_tsoa_s *restrict tgt, cots_ts_t UNUSED(s))
{
	if (UNLIKELY(tgt->toffs == NULL)) {
		return -1;
	}
	free(tgt->toffs);
	return 0;
}

ssize_t
cots_read_ticks(struct cots_tsoa_s *restrict tgt, cots_ts_t s)
{
/* currently this is mmap only */
	struct _ss_s *_s = (void*)s;
	const size_t blkz = _s->public.blockz;
	const size_t nflds = _s->public.nfields;
	const char *layo = _s->public.layout;
	size_t nrows;
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
	with (uint64_t zn) {
		size_t ntdcmp;

		memcpy(&zn, mp, sizeof(zn));
		zn = be64toh(zn);

		nrows = (zn & 0xffffffU) + 1U;
		rz = zn >> 24U;

		/* decompress */
		ntdcmp = dcmp(tgt, nflds, nrows, layo, mp + sizeof(zn), rz);
		if (UNLIKELY(ntdcmp != nrows)) {
			nrows = 0U;
		}

		rz += sizeof(zn);

		/* check footer */
		with (uint64_t zc) {
			memcpy(&zc, mp + rz, sizeof(zc));
			zc = be64toh(zc);
			if (zc >> 24U != rz) {
				/* too late now innit? */
				;
			}

			rz += sizeof(zc);
		}
	}

	/* unmap */
	munmap_any(mp, _s->ro, mz);

	/* advance iterator */
	_s->ro += rz;
	return nrows;
}


/* meta stuff */
static int
_bang_fields(struct _ss_s *_s, const char *flds, size_t fldz)
{
	const size_t nflds = _s->public.nfields;

	if (flds[fldz - 1U] != '\0') {
		/* that's bogus */
		return -1;
	} else if (_s->fields != NULL) {
		/* aha */
		free(_s->fields);
	}
	_s->fields = malloc(fldz);
	flds = memcpy(_s->fields, flds, fldz);

	/* bla, need to put shit into public fields */
	if (_s->public.fields == NULL) {
		_s->public.fields =
			calloc(nflds + 1U, sizeof(*_s->public.fields));

		if (_s->public.fields == NULL) {
			/* don't worry */
			return -1;
		}
	}
	with (const char **f = deconst(_s->public.fields)) {
		for (size_t i = 0U, fi = 0U; i < nflds && fi < fldz; i++) {
			f[i] = flds + fi;
			fi += strlen(flds + fi) + 1U;
		}
	}
	return 0;
}

int
cots_put_fields(cots_ts_t s, const char **fields)
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

cots_tag_t
cots_tag(cots_ts_t s, const char *str, size_t len)
{
	struct _ss_s *_s = (void*)s;
	return cots_intern(_s->ob, str, len);
}

const char*
cots_str(cots_ts_t s, cots_tag_t tag)
{
	struct _ss_s *_s = (void*)s;
	return _s->ob ? cots_tag_name(_s->ob, tag) : NULL;
}

/* cotse.c ends here */
