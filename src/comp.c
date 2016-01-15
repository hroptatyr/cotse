/*** comp.c -- cotse compactor
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
#include "cotse.h"
#include "comp.h"
#include "comp-to.h"
#include "comp-px.h"
#include "comp-qx.h"
#include "comp-ob.h"
#include "nifty.h"

#define ALGN16(x)	(void*)((uintptr_t)((x) + 0xfU) & ~0xfULL)


size_t
comp(uint8_t *tgt, size_t ncols, size_t nrows, const char *layout,
     const cots_to_t *to, const cots_tag_t *tag, void *const cols[])
{
	size_t totz = 0U;
	size_t z;

	z = comp_to(tgt + totz + sizeof(z), to, nrows);
	memcpy(tgt + totz, &z, sizeof(z));
	totz += z + sizeof(z);

	z = comp_tag(tgt + totz + sizeof(z), tag, nrows);
	memcpy(tgt + totz, &z, sizeof(z));
	totz += z + sizeof(z);

	/* columns now */
	for (size_t i = 0U; i < ncols; i++) {
		switch (layout[i]) {
		case COTS_LO_PRC:
		case COTS_LO_FLT: {
			uint32_t *c = cols[i];

			z = comp_px(tgt + totz + sizeof(z), c, nrows);
			memcpy(tgt + totz, &z, sizeof(z));
			totz += z + sizeof(z);
			break;
		}
		case COTS_LO_QTY:
		case COTS_LO_DBL: {
			uint64_t *c = cols[i];

			z = comp_qx(tgt + totz + sizeof(z), c, nrows);
			memcpy(tgt + totz, &z, sizeof(z));
			totz += z + sizeof(z);
			break;
		}
		default:
			break;
		}
	}
	return totz;
}

size_t
dcmp(cots_to_t *restrict t, cots_tag_t *restrict m, void *restrict cols[],
     size_t ncols, const char *layout, const uint8_t *src, size_t ssz)
{
	size_t si = 0U;
	size_t nt;
	size_t z;

	memcpy(&z, src + si, sizeof(z));
	si += sizeof(z);
	nt = dcmp_to(t, src + si, z);
	si += z;
	if (UNLIKELY(si >= ssz)) {
		return 0U;
	}

	memcpy(&z, src + si, sizeof(z));
	si += sizeof(z);
	if (dcmp_tag(m, src + si, z) != nt) {
		return 0U;
	}
	si += z;
	if (UNLIKELY(si >= ssz)) {
		return 0U;
	}

	/* columns now */
	for (size_t i = 0U; i < ncols; i++) {
		switch (layout[i]) {
		case COTS_LO_PRC:
		case COTS_LO_FLT: {
			uint32_t *c = cols[i];

			memcpy(&z, src + si, sizeof(z));
			si += sizeof(z);
			if (dcmp_px(c, src + si, z) != nt) {
				return 0U;
			}
			si += z;
			break;
		}
		case COTS_LO_QTY:
		case COTS_LO_DBL: {
			uint64_t *c = cols[i];

			memcpy(&z, src + si, sizeof(z));
			si += sizeof(z);
			if (dcmp_qx(c, src + si, z) != nt) {
				return 0U;
			}
			si += z;
			break;
		}
		default:
			break;
		}
	}
	return nt;
}

/* comp.c ends here */