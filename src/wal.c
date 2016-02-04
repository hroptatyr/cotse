/*** wal.c -- write-ahead logging
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
/**
 * Indices for cotse files are actually just timeseries themselves,
 * with a particular layout "(t)zz" */
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "cotse.h"
#include "wal.h"
#include "nifty.h"

#define MAP_MEM		(MAP_SHARED | MAP_ANON)
#define PROT_MEM	(PROT_READ | PROT_WRITE)
#ifndef MAP_ANON
# define MAP_ANON	MAP_ANONYMOUS
#endif	/* !MAP_ANON */

static inline __attribute__((const, pure)) size_t
_walz(const struct cots_wal_s *w)
{
	register const size_t blkz = w->blkz;
	register const size_t zrow = w->zrow;
	return blkz * zrow + sizeof(*w);
}

static inline void
_wal_init(struct cots_wal_s *restrict w, size_t zrow, size_t blkz)
{
	struct cots_wal_s proto = {"cots", "w0", COTS_ENDIAN, blkz, zrow};
	memcpy(w, &proto, sizeof(proto));
	return;
}


struct cots_wal_s*
_make_wal(size_t zrow, size_t blkz)
{
	const size_t z = zrow * blkz + sizeof(struct cots_wal_s);
	struct cots_wal_s *w;

	w = mmap(NULL, z, PROT_MEM, MAP_MEM, -1, 0);
	if (UNLIKELY(w == MAP_FAILED)) {
		return NULL;
	}
	/* otherwise */
	_wal_init(w, zrow, blkz);
	return w;
}

void
_free_wal(struct cots_wal_s *w)
{
	munmap(w, _walz(w));
	return;
}

struct cots_wal_s*
_wal_attach(const struct cots_wal_s *w, const char *fn)
{
	const size_t fz = _walz(w);
	struct cots_wal_s *res = NULL;
	int fd;

	if (UNLIKELY(fn == NULL)) {
		goto nul_out;
	}
	/* construct temp filename */
	with (size_t z = strlen(fn)) {
		char walfn[z + 5U];
		const int walfl = O_CREAT | O_TRUNC/*?*/ | O_RDWR;

		memcpy(walfn, fn, z);
		memcpy(walfn + z, ".wal", sizeof(".wal"));

		if (UNLIKELY((fd = open(walfn, walfl, 0666)) < 0)) {
			goto nul_out;
		}
	}
	if (UNLIKELY(ftruncate(fd, fz) < 0)) {
		goto clo_out;
	}
	/* map the file */
	res = mmap(NULL, fz, PROT_MEM, MAP_SHARED, fd, 0);
	if (UNLIKELY(res == MAP_FAILED)) {
		/* well done, just what we need */
		goto clo_out;
	}
	/* copy source wal */
	memcpy(res, w, fz);

clo_out:
	/* close the descriptor but leave the mapping */
	close(fd);
nul_out:
	return res;
}

int
_wal_detach(const struct cots_wal_s *w, const char *fn)
{
	if (UNLIKELY(fn == NULL)) {
		return -1;
	}
	_free_wal(deconst(w));
	/* construct temp filename */
	with (size_t z = strlen(fn)) {
		char walfn[z + 5U];

		memcpy(walfn, fn, z);
		memcpy(walfn + z, ".wal", sizeof(".wal"));

		unlink(walfn);
	}
	return 0;
}

/* wal.c ends here */
