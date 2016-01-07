/*** comp-qx.c -- compression routines for d64 quantities
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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cotse.h"
#include "comp-qx.h"
#include "pfor.h"
#include "dfp754_d64.h"
#include "nifty.h"

/* this should be at most 64U * P4DSIZE (cf. pfor.c)
 * so we can encode zigzag flags in one 64bit word */
#define MAX_NP		(8192U)
#define ROTBLK		(MAX_NP / 64U)


static void
xodt(uint64_t *restrict tgt, const cots_qx_t *restrict src, size_t np)
{
	tgt[0U] = bits64(src[0U]);
	for (size_t i = 1U; i < np; i++) {
		uint64_t x = 0U;
		/* delta */
		x ^= bits64(src[i - 1U]);
		x ^= bits64(src[i]);
		/* and store */
		tgt[i] = x;
	}
	return;
}

static void
xort(cots_qx_t *restrict tgt, const uint64_t *restrict src, size_t np)
{
	uint64_t sum = 0U;

	for (size_t i = 0U; i < np; i++) {
		/* sum up */
		sum ^= src[i];
		tgt[i] = bobs64(sum);
	}
	return;
}

static void
rolt(uint64_t *restrict io, size_t np)
{
	for (size_t i = 0U; i < np; i++) {
		/* rol it */
		io[i] = (io[i] << 1U) ^ (io[i] >> 63U);
	}
	return;
}

static void
rort(uint64_t *restrict io, size_t np)
{
	for (size_t i = 0U; i < np; i++) {
		/* ror it */
		io[i] = (io[i] >> 1U) ^ (io[i] << 63U);
	}
	return;
}

static uint64_t
rotmap(const uint64_t *v, size_t n)
{
/* count the number of set signum bits in V
 * in blocks of MAX_NP / 64U (which hopefully coincides with pfor.c's P4DSIZE)
 * return a 64bit word with the i-th bit set when block i should be
 * rotated, i.e. there's at least one value with the signum bits set. */
	uint64_t r = 0U;

	for (size_t b = 0U; b < 64U && b * ROTBLK + ROTBLK < n; b++) {
		uint64_t ns = 0U;

		for (size_t i = b * ROTBLK; i < b * ROTBLK + ROTBLK; i++) {
			ns |= v[i] >> 31U;
		}

		/* set bit when there's more than 12.5% of signa set */
		r ^= (ns & 0b1U) << b;
	}
	return r;
}


static size_t
_comp(uint8_t *restrict tgt, const cots_qx_t *restrict qx, size_t np)
{
	uint64_t pd[MAX_NP];
	uint64_t rm = 0U;
	size_t z = 0U;

	/* deltaify */
	xodt(pd, qx, np);
	/* number of sign changes */
	rm = rotmap(pd, np);
	for (unsigned int b = 0U; b < 64U; b++) {
		if (UNLIKELY((rm >> b) & 0b1U)) {
			rolt(pd + b * ROTBLK, ROTBLK);
		}
	}
	/* zigzag indicator (or flags in general) first */
	memcpy(tgt, &rm, sizeof(rm));
	z += sizeof(rm);
	z += pfor_enc64(tgt + z, pd, np);
	return z;
}

static size_t
_dcmp(cots_qx_t *restrict tgt, size_t np, const uint8_t *restrict c, size_t z)
{
	uint64_t pd[MAX_NP];
	size_t ci = 0U;
	uint64_t rm;
	(void)z;

	/* snarf flags, currently only zigzag */
	memcpy(&rm, c, sizeof(rm));
	ci += sizeof(rm);
	ci += pfor_dec64(pd, c + ci, np);

	/* use rotmap to unrotate things */
	for (unsigned int b = 0U; b < 64U; b++) {
		if (UNLIKELY((rm >> b) & 0b1U)) {
			rort(pd + b * ROTBLK, ROTBLK);
		}
	}
	/* and cumsum the whole thing */
	xort(tgt, pd, np);
	return ci;
}


/* compress */
size_t
comp_qx(uint8_t *restrict tgt, const cots_qx_t *restrict qx, size_t np)
{
	size_t res = 0U;

	memcpy(tgt, &np, sizeof(np));
	res += sizeof(np);
	for (size_t i = 0U; i < np; i += MAX_NP) {
		const size_t mt = MAX_NP < np - i ? MAX_NP : np - i;

		/* for small NP resort to dynarrs */
		res += _comp(tgt + res, qx + i, mt);
	}
	return res;
}

/* decompress */
size_t
dcmp_qx(cots_qx_t *restrict tgt, const uint8_t *restrict c, size_t nz)
{
	size_t ci = 0U;
	size_t np;

	memcpy(&np, c, sizeof(np));
	ci += sizeof(np);

	for (size_t i = 0U; i < np; i += MAX_NP) {
		const size_t mt = MAX_NP < np - i ? MAX_NP : np - i;

		/* small NPs are unpacked in _dcmp_small() */
		ci += _dcmp(tgt + i, mt, c + ci, nz - ci);
	}
	return np;
}

/* comp-qx.c ends here */
