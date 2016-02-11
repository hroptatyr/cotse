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
#include <math.h>
#include <assert.h>
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

/* page frames */
struct pagf_s {
	off_t beg;
	off_t end;
	unsigned int bits;
};

struct _ss_s {
	struct cots_ss_s public;

	/* mmapped file header */
	struct fhdr_s *mdr;

	/* compacted version of fields */
	char *fields;

	/* row-oriented page buffer, wal */
	struct cots_wal_s *wal;
	/* memory wal for swapsies,
	 * this will generally hold column-oriented wal and is
	 * updated from the row-WAL */
	struct cots_wal_s *mwal;

	/* currently attached file and its opening flags */
	int fd;
	int fl;
	/* current offset for next blob */
	off_t fo;
	/* current offset for reading */
	off_t ro;
	/* offset in ticks within the page */
	size_t rt;

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

static inline __attribute__((pure, const)) size_t
max_z(size_t x, size_t y)
{
	return x < y ? y : x;
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

static __attribute__((const, pure)) size_t
_layo_wid(const char lo)
{
	switch (lo) {
	case COTS_LO_BYT:
		return 1U;
	case COTS_LO_PRC:
	case COTS_LO_FLT:
		return 4U;
	case COTS_LO_TIM:
	case COTS_LO_CNT:
	case COTS_LO_STR:
	case COTS_LO_SIZ:
	case COTS_LO_QTY:
	case COTS_LO_DBL:
		return 8U;
	case COTS_LO_END:
	default:
		break;
	}
	return 0U;
}

static __attribute__((const, pure)) size_t
_layo_algn(size_t lz, size_t wid)
{
	switch (wid) {
	case 1U:
		break;
	case 2U:
		lz = ALGN2(lz);
		break;
	case 4U:
		lz = ALGN4(lz);
		break;
	case 8U:
	case 0U:
	default:
		lz = ALGN8(lz);
		break;
	}
	return lz;
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

		inc = _layo_wid(layout[i]);
		z = _layo_algn(z, inc);
	}
	return z;
}


/* _ss_s and cots_ts_t fiddlers */
static inline void
_inject_fn(cots_ts_t s, const char *fn)
{
	/* make backing file known */
	s->filename = strdup(fn);
	return;
}


static int
_bang_tsoa(
	uint8_t *restrict rows,
	const struct cots_tsoa_s *cols, size_t nrows,
	const char *flds, size_t nflds)
{
/* rowify NROWS values in COLS into ROWS according to FLDS */
	const size_t zrow = _algn_zrow(flds, nflds);

	/* bang toffs */
	for (size_t j = 0U; j < nrows; j++) {
		memcpy(rows + j * zrow + 0U,
		       cols->toffs + j, sizeof(*cols->toffs));
	}
	for (size_t i = 0U, a = _algn_zrow(flds, i), wid = 0U; i < nflds; i++) {
		uint8_t *cp = cols->cols[i];

		/* get current field's width and alignment */
		a += wid;
		wid = _layo_wid(flds[i]);
		a = _layo_algn(a, wid);
		for (size_t j = 0U; j < nrows; j++) {
			memcpy(rows + j * zrow + a, cp + j * wid, wid);
		}
	}
	return 0;
}

static int
_bang_tick(
	struct cots_tsoa_s *restrict cols,
	const uint8_t *rows, size_t nrows,
	const char *flds, size_t nflds,
	size_t ot)
{
/* columnarise NROWS values in ROWS into COLS accordings to FLDS
 * assume OT rows have been written already */
	const size_t zrow = _algn_zrow(flds, nflds);


	/* columnarise times */
	for (size_t j = ot; j < nrows; j++) {
		memcpy(cols->toffs + j,
		       rows + j * zrow + 0U, sizeof(*cols->toffs));
	}

	/* columnarise the rest */
	for (size_t i = 0U, a = _algn_zrow(flds, i), wid = 0U; i < nflds; i++) {
		uint8_t *c = cols->cols[i];

		/* get current field's width and alignment */
		a += wid;
		wid = _layo_wid(flds[i]);
		a = _layo_algn(a, wid);
		for (size_t j = ot; j < nrows; j++) {
			memcpy(c + j * wid, rows + j * zrow + a, wid);
		}
	}
	return 0;
}

static struct blob_s
_make_blob(
	const char *flds, size_t nflds,
	const struct cots_wal_s *src, struct cots_wal_s *restrict tmp)
{
	const size_t blkz = src->blkz;
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
	uint64_t z;

	if (UNLIKELY(!(nrows = _wal_rowi(src)))) {
		/* trivial */
		return (struct blob_s){0U, NULL};
	}

	/* trial mmap, fair estimate would be to use nrows * nflds * uint64_t
	 * and then time 2 for the compressed data as well */
	bi = sizeof(uint64_t) * (nflds + 1U) * (nrows > 64U ? nrows : 64U);
	bsz = 2U * bi;
	buf = mmap(NULL, bsz, PROT_MEM, MAP_MEM, -1, 0);
	if (buf == MAP_FAILED) {
		return (struct blob_s){0U, NULL};
	}

	/* imprint standard layout on COLS tsoa using mwal's buffer */
	cols.proto.toffs = (void*)tmp->data;
	for (size_t i = 0U; i < nflds; i++) {
		const size_t a = _algn_zrow(flds, i);
		cols.cols[i] = tmp->data + blkz * a;
	}
	/* call the columnifier */
	_bang_tick(&cols.proto, src->data, nrows, flds, nflds, _wal_rowi(tmp));
	_wal_rset(tmp, nrows);

	/* get from and till values */
	cols.from = cols.proto.toffs[0U];
	cols.till = cols.proto.toffs[nrows - 1U];

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
	const size_t blkz = _s->wal->blkz - 1U;
	const size_t rowi = (_wal_rowi(_s->wal) - 1U) & blkz;

	/* this assumes the wal to be a ring buffer and/or
	 * flush copying the last tick to the end of the buffer */
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

	/* just to be sure where we're writing things */
	(void)lseek(_s->fd, _s->fo, SEEK_SET);
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
_rd_meta(struct _ss_s *restrict _s)
{
	struct chnk_s c;
	size_t mz;
	uint8_t *m;

	if (UNLIKELY(_s->fd < 0)) {
		/* not doing no-disk-backed shit */
		return -1;
	} else if (UNLIKELY(_s->mdr == NULL)) {
		/* someone forgot to map the header */
		return -1;
	}

	const off_t moff = be64toh(_s->mdr->moff);
	const off_t noff = be64toh(_s->mdr->noff);

	if (UNLIKELY(moff >= noff)) {
		/* file must be fuckered */
		return -1;
	}
	/* try mapping him */
	m = mmap_any(_s->fd, PROT_READ, MAP_SHARED, moff, mz = noff - moff);
	if (UNLIKELY(m == NULL)) {
		/* it's no good */
		return -1;
	}
	/* try reading him */
	for (size_t mi = 0U;
	     mi < mz && (c = _rd_meta_chnk(m + mi, mz - mi)).data;
	     mi = c.data + c.z - m) {
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

	/* don't leave a trace */
	(void)munmap_any(m, moff, noff - moff);
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
	b = _make_blob(layo, nflds, _s->wal, _s->mwal);

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
	/* devance read offset */
	if (UNLIKELY(_s->ro > _s->fo)) {
		_s->ro = _s->fo + b.z;
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
	/* keep last wal value */
	with (uint64_t bak[nflds + 1U]) {
		_wal_last(bak, _s->wal);
		_wal_rset(_s->wal, -1ULL);
		_wal_bang(_s->wal, bak);
	}
rst_out:
	/* and reset both WALs */
	_wal_rset(_s->wal, 0U);
	_wal_rset(_s->mwal, 0U);
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

static ssize_t
_rd_cpag(struct cots_tsoa_s *restrict tgt,
	 const int fd, off_t *restrict o, const size_t z,
	 const char *layo, size_t nflds)
{
	const uint8_t *p;
	size_t nrows;
	size_t rz;

	p = mmap_any(fd, PROT_READ, MAP_SHARED, *o, z);
	if (UNLIKELY(p == NULL)) {
		/* don't bother updating offset either */
		return -1;
	}

	/* quickly inspect integrity, well update RO more importantly */
	with (uint64_t zn) {
		size_t ntdcmp;

		memcpy(&zn, p, sizeof(zn));
		zn = be64toh(zn);

		nrows = (zn & 0xffffffU) + 1U;
		rz = zn >> 24U;

		if (UNLIKELY(rz > z)) {
			/* more space than is mapped? */
			nrows = 0U;
			rz = 0U;
			break;
		}
		/* decompress */
		ntdcmp = dcmp(tgt, nflds, nrows, layo, p + sizeof(zn), rz);
		if (UNLIKELY(ntdcmp != nrows)) {
			nrows = 0U;
			rz = 0U;
			break;
		}

		rz += 2U * sizeof(zn);
	}

	/* unmap */
	munmap_any(deconst(p), *o, z);
	/* and update offset */
	*o += rz;
	return nrows;
}

static size_t
_rd_layo(const char **layo, int fd, off_t at)
{
	size_t lz, li = 0U;
	char *lp;

	/* start out with a conservative 7 fields */
	if (UNLIKELY((lp = malloc(lz = 8U)) == NULL)) {
		return 0U;
	}
	while (1) {
		ssize_t nrd = pread(fd, lp + li, lz - li, at + li);
		const char *eo;

		if (UNLIKELY(nrd <= 0)) {
			/* no luck whatsoever */
			goto nul_out;
		} else if ((eo = memchr(lp + li, '\0', nrd)) != NULL) {
			li = eo - lp;
			break;
		}
		/* otherwise double in size and retry */
		li += nrd;
		lp = realloc(lp, lz *= 2U);
	}
	/* very good, count and assign */
	*layo = lp;
	return li;

nul_out:
	free(lp);
	return 0U;
}

static cots_ts_t
_open_core(int fd, struct orng_s r)
{
/* treat range R.BEG till R.END as cots_ts and try and open it */
	struct _ss_s *res;
	struct fhdr_s hdr;
	const char *layo;
	size_t nflds;
	size_t blkz;

	if (UNLIKELY(r.end - r.beg < (ssize_t)sizeof(*res->mdr))) {
		return NULL;
	} else if (pread(fd, &hdr, sizeof(hdr), r.beg) < (ssize_t)sizeof(hdr)) {
		/* header bit b0rked */
		return NULL;
	}
	/* inspect header */
	if (memcmp(hdr.magic, "cots", sizeof(hdr.magic))) {
		/* nope, better fuck off then */
		return NULL;
	}
	/* read block size */
	with (uint64_t hfl = be64toh(hdr.flags)) {
		blkz = exp_lgbz(hfl & 0xfU);
	}
	/* snarf the layout and calculate zrow size */
	nflds = _rd_layo(&layo, fd, r.beg + sizeof(hdr));


	/* construct the result object */
	if (UNLIKELY((res = calloc(1, sizeof(*res))) == NULL)) {
		return NULL;
	}

	/* make number of fields known publicly */
	with (void *nfp = deconst(&res->public.nfields)) {
		memcpy(nfp, &nflds, sizeof(nflds));
		/* also keep layout for reference */
		res->public.layout = layo;
	}
	/* make blocksize known publicly */
	with (void *bzp = deconst(&res->public.blockz)) {
		memcpy(bzp, &blkz, sizeof(blkz));
	}

	/* map the header for reference */
	res->mdr = mmap_any(fd, PROT_READ, MAP_SHARED, r.beg, _hdrz(res));
	if (UNLIKELY(res->mdr == NULL)) {
		goto fre_out;
	}

	/* collect details about this backing file */
	res->fd = fd;
	res->fl = O_RDONLY;
	res->fo = r.beg + be64toh(res->mdr->moff) ?: r.end;
	res->ro = r.beg + _hdrz(res);

	/* short dip into the meta pool */
	(void)_rd_meta(res);

	/* use a backing file */
	return (cots_ts_t)res;

fre_out:
	free(res);
	return NULL;
}

static int
_yank_rng(const char *fn, int fd, struct orng_s r)
{
/* assume stuff between R.BEG and R.END is an index series
 * cut that out and write to separate file FN. */
	const int fl = O_CREAT | O_TRUNC/*?*/ | O_RDWR;
	int resfd;

	if (UNLIKELY(r.beg >= r.end)) {
		/* yeah right */
		return -1;
	} else if (UNLIKELY((resfd = open(fn, fl, 0666)) < 0)) {
		/* pass on the error */
		return -1;
	}
	/* copy range */
	do {
		ssize_t nsf;

		nsf = sendfile(resfd, fd, &r.beg, r.end - r.beg);
		if (UNLIKELY(nsf < 0)) {
			/* no way */
			(void)unlink(fn);
			close(resfd);
			resfd = -1;
			break;
		}
	} while (r.beg < r.end);
	return resfd;
}

static int
_open_idx(struct _ss_s *_s, off_t eo)
{
	struct orng_s at = {0, eo};

	do {
		off_t noff = be64toh(_s->mdr->noff);

		if (UNLIKELY(!noff || (at.beg += noff) >= at.end)) {
			break;
		}
		_s->idx = _open_core(_s->fd, at);
	} while ((_s = (void*)_s->idx));
	return 0;
}

static struct pagf_s
_prev_pg(int fd, off_t at)
{
/* given an offset in FD and assuming a page precedes it immediately
 * return the page's offsets (in octets) within FD
 * wrt the beginning of the file */
	uint64_t z;
	unsigned int b;

	at -= sizeof(uint64_t);
	if (UNLIKELY(pread(fd, &z, sizeof(z), at) < (ssize_t)sizeof(z))) {
		return (struct pagf_s){0};
	}
	/* nativendianify */
	z = be64toh(z);
	/* and massage because it also contains a crc24 */
	b = z & 0xffffffU;
	z >>= 24U;
	return (struct pagf_s){at - z, at, b};
}

static struct pagf_s
_next_pg(int fd, off_t at)
{
/* given an offset in FD and assuming a page starts there immediately
 * return the page's offsets (in octets) within FD
 * wrt the beginning of the file */
	uint64_t z;
	unsigned int b;

	if (UNLIKELY(pread(fd, &z, sizeof(z), at) < (ssize_t)sizeof(z))) {
		return (struct pagf_s){0};
	}
	/* nativendianify */
	z = be64toh(z);
	/* and massage because it contains the tick count as well */
	b = z & 0xffffffU;
	z >>= 24U;
	return (struct pagf_s){at, at + z + sizeof(uint64_t), b};
}

static int
_yank_wal(struct _ss_s *_s, off_t eo)
{
/* examine last page of _S, create a WAL and bang stuff there */
	const char *layo = _s->public.layout;
	const size_t nflds = _s->public.nfields;
	const size_t blkz = _s->public.blockz;
	const size_t zrow = _algn_zrow(layo, nflds);
	struct cots_wal_s *res;
	struct pagf_s f;
	size_t nt;

	f = _prev_pg(_s->fd, _s->fo);
	if (UNLIKELY(f.beg >= f.end)) {
		/* empty range is not really WAL-worthy is it? */
		return -1;
	} else if (UNLIKELY(f.end >= eo)) {
		/* page ends behind end-of-file? yeah, right! */
		return -1;
	}
	/* cross check and get number of ticks*/
	with (struct pagf_s nf = _next_pg(_s->fd, f.beg)) {
		/* compare the orng_s portion because the bits might differ */
		if (UNLIKELY(memcmp(&f, &nf, sizeof(struct orng_s)))) {
			/* that's just not on */
			return -1;
		}
		/* yay, snarf them ticks */
		nt = nf.bits + 1U;
	}

	/* get some breathing space */
	with (const char *fn = _s->public.filename) {
		assert(_s->mwal == NULL);
		if (UNLIKELY((res = _wal_create(zrow, blkz, fn)) == NULL)) {
			return -1;
		} else if ((_s->mwal = _make_wal(zrow, blkz)) == NULL) {
			goto wal_out;
		}
	}

	/* check if page is non-full, if so read+decomp it */
	if (LIKELY(nt < blkz)) {
		struct {
			union {
				struct cots_tsoa_s t;
				void *toffs;
			};
			void *flds[nflds];
		} tgt;
		off_t o = f.beg;
		ssize_t ntrd;

		/* set up target with the mwal */
		tgt.toffs = _s->mwal->data;
		for (size_t i = 0U; i < nflds; i++) {
			const size_t a = _algn_zrow(layo, i);
			tgt.flds[i] = _s->mwal->data + a * blkz;
		}

		ntrd = _rd_cpag(&tgt.t, _s->fd, &o, f.end - f.beg, layo, nflds);
		if (UNLIKELY(ntrd < 0)) {
			goto wal_mwal_out;
		} else if (UNLIKELY(ntrd != nt)) {
			/* shouldn't we feel sorry and accept at least
			 * the number of read ticks? */
			goto wal_mwal_out;
		}
		/* increment column-WAL row counter to NT */
		_wal_rset(_s->mwal, nt);

		/* wind back file offset, we'll truncate later */
		_s->fo = f.beg;

		/* rowify wal */
		_bang_tsoa(res->data, &tgt.t, nt, layo, nflds);
		/* increment to WAL to NT */
		_wal_rset(res, nt);
	}
	/* otherwise don't read anything back, go with a clean WAL */
	_s->wal = res;
	return 0;

wal_mwal_out:
	_free_wal(_s->mwal);
wal_out:
	_free_wal(res);
	return -1;
}

static int
_move_idx(struct _ss_s *_s, off_t eo)
{
	size_t flen = strlen(_s->public.filename);
	/* estimate how many index sections there will be,
	 * we'd say there's at most 1kB of index for 10kB of data,
	 * so simply guesstimate the number as log10 of size minus 3 */
	const size_t idepth = max_z(log10((double)eo) - 3, 0U);
	/* reserve string space */
	char ifn[flen + strlenof(".idx") * idepth + 1U];
	struct orng_s irng = {.end = eo};

	memcpy(ifn, _s->public.filename, flen);
	for (size_t i = 0U;
	     i < idepth && (irng.beg = be64toh(_s->mdr->noff));
	     i++, _s = (void*)_s->idx) {
		int ifd;

		/* construct a new temp file name */
		memcpy(ifn + flen + i * strlenof(".idx"),
		       ".idx", sizeof(".idx"));
		if ((ifd = _yank_rng(ifn, _s->fd, irng)) < 0) {
			break;
		}
		/* try and read this as a cots file*/
		irng.end -= irng.beg, irng.beg = 0;
		if ((_s->idx = _open_core(ifd, irng)) == NULL) {
			close(ifd);
			break;
		}
		/* memorise temp file name */
		_inject_fn(_s->idx, ifn);

		with (struct _ss_s *_sidx = (void*)_s->idx) {
			size_t ni;

			/* reprotect the index's header */
			(void)mprot_any(_sidx->mdr, 0, _hdrz(_sidx), PROT_MEM);

			/* also yank last bob into WAL */
			if (_yank_wal(_sidx, irng.end - irng.beg) < 0) {
				break;
			} else if (!(ni = _wal_rowi(_sidx->wal))) {
				break;
			}
			/* and wind back the counter */
			_wal_rset(_sidx->wal, --ni);
		}
	}
	return 0;
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
	struct stat st;
	cots_ts_t res;
	off_t eo;
	int fd;

	if ((fd = open(file, flags ? O_RDWR : O_RDONLY)) < 0) {
		return NULL;
	} else if (UNLIKELY(fstat(fd, &st) < 0)) {
		goto clo_out;
	} else if (UNLIKELY((eo = st.st_size) <= 0)) {
		/* nothing to open here, is there */
		goto clo_out;
	} else if ((res = _open_core(fd, (struct orng_s){0, eo})) == NULL) {
		goto clo_out;
	}

	/* make backing file known */
	_inject_fn(res, file);

	if (flags == O_RDONLY) {
		/* do up the index, coupling! */
		struct _ss_s *_res = (void*)res;

		_open_idx(_res, eo);
	} else {
		/* right, dissect file, put index into separate file
		 * and do that recursively */
		struct _ss_s *_res = (void*)res;

		/* pass on the right flags */
		_res->fl = flags;
		/* read indices and move them */
		_move_idx(_res, eo);
		/* turn contents of last page into WAL */
		_yank_wal(_res, eo);
		/* switch off header write protection */
		(void)mprot_any(_res->mdr, 0, _hdrz(_res), PROT_MEM);
	}
	return res;

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
		(void)mprot_any(mdr, 0, hz, PROT_MEM);
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
		assert(_s->mwal);
		_s->wal = _s->mwal;
		_s->mwal = NULL;
		/* assume flushed wal */
		_wal_rset(_s->wal, 0U);
	}
	if (_s->idx) {
		/* assume index has been dealt with in _freeze() */
		if (_s->fl != O_RDONLY) {
			free_cots_idx(_s->idx);
		} else {
			free_cots_ts(_s->idx);
		}
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
	}

	/* flush wal to file */
	rc = _flush(_s);

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
	size_t mz;
	size_t nr;

	if (UNLIKELY(_s->fd < 0)) {
		/* no backing file */
		return -1;
	} else if (UNLIKELY(_s->ro >= _s->fo) && _s->mwal) {
		/* no compressed ticks on their pages, innit? */
		goto _wal_read_ticks;
	} else if (UNLIKELY(_s->ro >= _s->fo)) {
		return 0;
	}

	/* guesstimate the page that needs mapping */
	mz = min_z(_s->fo - _s->ro, blkz * nflds * sizeof(uint64_t));
	/* and read/decomp the page */
	nr = _rd_cpag(tgt, _s->fd, &_s->ro, mz, layo, nflds);
	if (LIKELY(!_s->rt)) {
		return nr;
	}
	/* otherwise it's extra-time, kill the first _s->rt ticks then */
	assert(_s->rt <= nr);
	/* adapt result value */
	nr -= _s->rt;
	/* start with the time offsets */
	memmove(tgt->toffs, tgt->toffs + _s->rt, nr * sizeof(*tgt->toffs));
	for (size_t i = 0U, a = _algn_zrow(layo, i), b; i < nflds; i++, b = a) {
		uint8_t *tp = tgt->cols[i];
		const size_t wid = (b = _algn_zrow(layo, i + 1U), b - a);
		memmove(tp, tp + _s->rt * wid, nr * wid);
	}
	_s->rt += nr;
	_s->rt &= (blkz - 1U);
	return nr;

_wal_read_ticks:
	nr = _wal_rowi(_s->mwal);
	size_t nt = _wal_rowi(_s->wal);

	if (nt > nr) {
		struct {
			struct cots_tsoa_s proto;
			void *cols[nflds];
		} cols;

		/* imprint standard layout on COLS tsoa */
		cols.proto.toffs = (void*)_s->mwal->data;
		for (size_t i = 0U; i < nflds; i++) {
			const size_t a = _algn_zrow(layo, i);
			cols.cols[i] = _s->mwal->data + blkz * a;
		}

		/* columnify again */
		_bang_tick(&cols.proto, _s->wal->data, nt, layo, nflds, nr);
		_wal_rset(_s->mwal, nt);
	} else if (UNLIKELY(_s->rt >= nr)) {
		return 0;
	}
	/* we'd be writing NT ticks, offset at _S->RT */
	nt -= _s->rt;
	/* time vector first */
	with (const uint8_t *sp = _s->mwal->data) {
		const size_t wid = sizeof(*tgt->toffs);
		memcpy(tgt->toffs, sp + _s->rt * wid, nt * wid);
	}
	for (size_t i = 0U, a = _algn_zrow(layo, i), b; i < nflds; i++, a = b) {
		const uint8_t *sp = _s->mwal->data + a * blkz;
		const size_t wid = (b = _algn_zrow(layo, i + 1U), b - a);
		memcpy(tgt->cols[i], sp + _s->rt * wid, nt * wid);
	}
	_s->rt += nt;
	return nt;
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


#if defined TESTING
#include "munit.h"

int main(void)
{
	size_t nfailed = 0U;

	munit_assert_size(_layo_algn(8, 4), ==, 8, nfailed++);
	munit_assert_size(_layo_algn(10, 4), ==, 12, nfailed++);
	munit_assert_size(_layo_algn(10, 2), ==, 10, nfailed++);
	munit_assert_size(_layo_algn(12, 8), ==, 16, nfailed++);
	munit_assert_size(_layo_algn(17, 1), ==, 17, nfailed++);
	munit_assert_size(_layo_algn(17, 2), ==, 18, nfailed++);
	munit_assert_size(_layo_algn(17, 4), ==, 20, nfailed++);
	munit_assert_size(_layo_algn(17, 0), ==, 24, nfailed++);

	munit_assert_size(_algn_zrow("pq", 0U), ==, 8, nfailed++);
	munit_assert_size(_algn_zrow("pq", 1U), ==, 16, nfailed++);
	munit_assert_size(_algn_zrow("pq", 2U), ==, 24, nfailed++);

	munit_assert_size(_algn_zrow("qp", 0U), ==, 8, nfailed++);
	munit_assert_size(_algn_zrow("qp", 1U), ==, 16, nfailed++);
	munit_assert_size(_algn_zrow("qp", 2U), ==, 24, nfailed++);

	munit_assert_size(_algn_zrow("pp", 0U), ==, 8, nfailed++);
	munit_assert_size(_algn_zrow("pp", 1U), ==, 12, nfailed++);
	munit_assert_size(_algn_zrow("pp", 2U), ==, 16, nfailed++);

	munit_assert_size(_algn_zrow("ppp", 0U), ==, 8, nfailed++);
	munit_assert_size(_algn_zrow("ppp", 1U), ==, 12, nfailed++);
	munit_assert_size(_algn_zrow("ppp", 2U), ==, 16, nfailed++);
	munit_assert_size(_algn_zrow("ppp", 3U), ==, 24, nfailed++);

	munit_assert_size(_algn_zrow("pqp", 0U), ==, 8, nfailed++);
	munit_assert_size(_algn_zrow("pqp", 1U), ==, 16, nfailed++);
	munit_assert_size(_algn_zrow("pqp", 2U), ==, 24, nfailed++);
	munit_assert_size(_algn_zrow("pqp", 3U), ==, 32, nfailed++);

	munit_assert_size(_algn_zrow("ppq", 0U), ==, 8, nfailed++);
	munit_assert_size(_algn_zrow("ppq", 1U), ==, 12, nfailed++);
	munit_assert_size(_algn_zrow("ppq", 2U), ==, 16, nfailed++);
	munit_assert_size(_algn_zrow("ppq", 3U), ==, 24, nfailed++);

	return !nfailed ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif	/* TESTING */

/* cotse.c ends here */
