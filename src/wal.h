/*** wal.h -- write-ahead logging
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
#if !defined INCLUDED_wal_h_
#define INCLUDED_wal_h_
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "cotse.h"

/* disk representation of the WAL */
struct cots_wal_s {
	/* should be "cots" */
	const uint8_t magic[4U];
	/* should be "w0" */
	const uint8_t version[2U];
	/* COTS_ENDIAN written in native endian */
	const uint16_t endian;
	/* block size in bytes and native endian */
	uint64_t blkz;
	/* row size in bytes and native endian */
	const uint64_t zrow;
	/* row index in native endian */
	uint64_t rowi;
	/* the ordinary data, aligned on a 16 byte boundary
	 * written in native endian */
	uint8_t data[];
};


extern struct cots_wal_s *_make_wal(size_t zrow, size_t blkz);
extern void _free_wal(struct cots_wal_s *w);

extern struct cots_wal_s*
_wal_attach(const struct cots_wal_s *w, const char *fn);

extern int
_wal_detach(const struct cots_wal_s *w, const char *fn);


static inline __attribute__((const)) size_t
_wal_rowi(const struct cots_wal_s *w)
{
/* return row index in native form */
	return w->rowi;
}

static inline void
_wal_rset(struct cots_wal_s *w, size_t n)
{
/* reset row index */
	w->rowi = n;
	return;
}

static inline size_t
_wal_rinc(struct cots_wal_s *w)
{
	return ++w->rowi;
}

static inline void
_wal_bang(struct cots_wal_s *w, const void *data)
{
	register const size_t rowi = _wal_rowi(w);
	register const size_t zrow = w->zrow;
	memcpy(w->data + rowi * zrow, data, zrow);
	return;
}

#endif	/* INCLUDED_wal_h_ */
