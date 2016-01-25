/*** comp-px.c -- compression routines for hash values
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
#include "comp-ob.h"
#include "pfor.h"
#include "dfp754_d32.h"
#include "nifty.h"

#define MAX_NM		(8192U)

static void
xodt(uint64_t *restrict tgt, const cots_tag_t *restrict src, size_t nm)
{
	tgt[0U] = src[0U];
	for (size_t i = 1U; i < nm; i++) {
		uint64_t x;

		/* delta */
		x = src[i - 1U] ^ src[i];
		/* and store */
		tgt[i] = x;
	}
	return;
}

static void
xort(cots_tag_t *restrict tgt, const uint64_t *restrict src, size_t nm)
{
	uint64_t sum = 0U;

	for (size_t i = 0U; i < nm; i++) {
		/* sum up */
		sum ^= src[i];
		tgt[i] = sum;
	}
	return;
}


static size_t
_comp(uint8_t *restrict tgt, const cots_tag_t *restrict qx, size_t nm)
{
	uint64_t pd[MAX_NM];
	size_t z = 0U;

	/* deltaify */
	xodt(pd, qx, nm);
	z += pfor_enc64(tgt + z, pd, nm);
	return z;
}

static size_t
_dcmp(cots_tag_t *restrict tgt, size_t nm, const uint8_t *restrict c, size_t z)
{
	uint64_t pd[MAX_NM];
	size_t ci = 0U;
	(void)z;

	ci += pfor_dec64(pd, c + ci, nm);
	/* and cumsum the whole thing */
	xort(tgt, pd, nm);
	return ci;
}


/* compress */
size_t
comp_tag(uint8_t *restrict tgt, const cots_tag_t *restrict m, size_t nm)
{
	size_t res = 0U;

	for (size_t i = 0U; i < nm; i += MAX_NM) {
		const size_t mm = MAX_NM < nm - i ? MAX_NM : nm - i;

		/* no further filtering needed, just use pfor */
		res += _comp(tgt + res, m + i, mm);
	}
	return res;
}

/* decompress */
size_t
dcmp_tag(cots_tag_t *restrict tgt, size_t nt, const uint8_t *c, size_t nz)
{
	size_t ci = 0U;

	for (size_t i = 0U; i < nt; i += MAX_NM) {
		const size_t mt = MAX_NM < nt - i ? MAX_NM : nt - i;

		/* no further filtering needed, just use pfor */
		ci += _dcmp(tgt + i, mt, c + ci, nz - ci);
	}
	return nt;
}

/* comp-ob.c ends here */
