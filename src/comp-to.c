/*** comp-to.c -- compression routines for the time axis
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
#include "comp-to.h"
#include "pfor.h"
#include "nifty.h"

#define MAX_NT		(8192U)


static cots_to_t
avgt(const cots_to_t *t, size_t nt)
{
/* return most prominent t-delta */
	double sum = 0.;

	for (size_t i = 1U; i < nt; i++) {
		sum += log10((double)(t[i] ?: 1ULL));
	}
	sum = exp10(sum / (double)(nt - 1U));
	return lrint(sum);
}

static void
delt(cots_to_t *restrict tgt, const cots_to_t *restrict src, size_t nt)
{
	tgt[0U] = src[0U];
	for (size_t i = 1U; i < nt; i++) {
		tgt[i] = src[i] - src[i - 1U];
	}
	return;
}

static void
sumt(cots_to_t *restrict io, size_t nt)
{
	for (size_t i = 1U; i < nt; i++) {
		io[i] += io[i - 1U];
	}
	return;
}

static void
rmavgt(cots_to_t *restrict io, size_t nt, cots_to_t avg)
{
	for (size_t i = 0U; i < nt; i++) {
		long int x = io[i] - avg;
		/* and zig-zag encode him */
		io[i] = (x << 1U) ^ (x >> 63);
	}
	return;
}

static void
adavgt(cots_to_t *restrict io, size_t nt, cots_to_t avg)
{
	for (size_t i = 0U; i < nt; i++) {
		/* zig-zag decode him */
		long int x = (io[i] >> 1U) ^ (-(io[i] & 0b1U));
		io[i] = x + avg;
	}
	return;
}

static size_t
_comp(uint8_t *restrict tgt, const cots_to_t *restrict to, size_t nt)
{
	cots_to_t td[MAX_NT];
	cots_to_t avg;
	size_t z = 0U;

	/* deltaify */
	delt(td, to, nt);
	/* estimate average delta */
	avg = avgt(td, nt);
	/* kill the average */
	rmavgt(td, nt, avg);
	/* store average, endian? */
	memcpy(tgt, &avg, sizeof(avg));
	z += sizeof(avg);
	z += pfor_enc64(tgt + z, td, nt);
	return z;
}

static size_t
_dcmp(cots_to_t *restrict tgt, size_t nt, const uint8_t *restrict c, size_t z)
{
	cots_to_t avg;
	size_t ci = 0U;
	(void)z;

	/* snarf average value */
	memcpy(&avg, c, sizeof(avg));
	ci += sizeof(avg);
	ci += pfor_dec64(tgt, c + ci, nt);

	/* add average too */
	adavgt(tgt, nt, avg);
	/* and cumsum the whole thing */
	sumt(tgt, nt);
	return ci;
}


/* compress */
size_t
comp_to(uint8_t *restrict tgt, const cots_to_t *restrict to, size_t nt)
{
	size_t res = 0U;

	memcpy(tgt, &nt, sizeof(nt));
	res += sizeof(nt);
	for (size_t i = 0U; i < nt; i += MAX_NT) {
		const size_t mt = MAX_NT < nt - i ? MAX_NT : nt - i;

		/* for small NT resort to dynarrs */
		res += _comp(tgt + res, to + i, mt);
	}
	return res;
}

/* decompress */
size_t
dcmp_to(cots_to_t *restrict tgt, const uint8_t *c, size_t nz)
{
	size_t ci = 0U;
	size_t nt;

	memcpy(&nt, c, sizeof(nt));
	ci += sizeof(nt);

	for (size_t i = 0U; i < nt; i += MAX_NT) {
		const size_t mt = MAX_NT < nt - i ? MAX_NT : nt - i;

		/* small NTs are unpacked in _dcmp_small() */
		ci += _dcmp(tgt + i, mt, c + ci, nz - ci);
	}
	return nt;
}

/* comp-to.c ends here */
