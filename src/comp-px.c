/*** comp-px.c -- compression routines for d32 prices
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
#include "comp-px.h"
#include "pfor.h"
#include "nifty.h"

/* this should be at most 64U * P4DSIZE (cf. pfor.c)
 * so we can encode zigzag flags in one 64bit word */
#define MAX_NP		(8192U)
#define ROTBLK		(MAX_NP / 64U)


static void
xodt(uint32_t *restrict tgt, const uint32_t *restrict src, size_t np)
{
	tgt[0U] = src[0U];
	for (size_t i = 1U; i < np; i++) {
		/* delta */
		tgt[i] = src[i - 1U] ^ src[i];
	}
	return;
}

static void
xort(uint32_t *restrict tgt, const uint32_t *restrict src, size_t np)
{
	tgt[0U] = src[0U];
	for (size_t i = 1U; i < np; i++) {
		/* sum up */
		tgt[i] = tgt[i - 1U] ^ src[i];
	}
	return;
}


static size_t
_comp(uint8_t *restrict tgt, const uint32_t *restrict px, size_t np)
{
	uint32_t pd[MAX_NP];
	uint16_t nibl[sizeof(pd) / sizeof(uint16_t)];
	size_t z = 0U;

	/* deltaify */
	xodt(pd, px, np);

	/* nibblify */
	for (size_t i = 0U; i < np; i++) {
		nibl[i + 0U * MAX_NP] = (uint16_t)((pd[i] >> 0U));
		nibl[i + 1U * MAX_NP] = (uint16_t)((pd[i] >> 16U));
	}

	z += pfor_enc16(tgt + z, nibl + 0U * MAX_NP, np);
	z += pfor_enc16(tgt + z, nibl + 1U * MAX_NP, np);
	return z;
}

static size_t
_dcmp(uint32_t *restrict tgt, size_t np, const uint8_t *restrict c, size_t z)
{
	uint32_t pd[MAX_NP];
	uint16_t nibl[sizeof(pd) / sizeof(uint16_t)];
	size_t ci = 0U;
	(void)z;

	ci += pfor_dec16(nibl + 0U * MAX_NP, c + ci, np);
	ci += pfor_dec16(nibl + 1U * MAX_NP, c + ci, np);

	/* reassemble 32bit words */
	for (size_t i = 0U; i < np; i++) {
		pd[i] = (uint32_t)nibl[i + 0U * MAX_NP];
		pd[i] ^= (uint32_t)nibl[i + 1U * MAX_NP] << 16U;
	}

	/* and cumsum the whole thing */
	xort(tgt, pd, np);
	return ci;
}


/* compress */
size_t
comp_px(uint8_t *restrict tgt, const uint32_t *restrict px, size_t np)
{
	size_t res = 0U;

	for (size_t i = 0U; i < np; i += MAX_NP) {
		const size_t mt = MAX_NP < np - i ? MAX_NP : np - i;

		/* for small NP resort to dynarrs */
		res += _comp(tgt + res, px + i, mt);
	}
	return res;
}

/* decompress */
size_t
dcmp_px(uint32_t *restrict tgt, size_t nt, const uint8_t *restrict c, size_t nz)
{
	size_t ci = 0U;

	for (size_t i = 0U; i < nt; i += MAX_NP) {
		const size_t mt = MAX_NP < nt - i ? MAX_NP : nt - i;

		/* small NPs are unpacked in _dcmp_small() */
		ci += _dcmp(tgt + i, mt, c + ci, nz - ci);
	}
	return nt;
}

/* comp-px.c ends here */
