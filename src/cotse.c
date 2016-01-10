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
#include <stdarg.h>
#include <stdlib.h>
#include "cotse.h"
#include "comp-to.h"
#include "comp-px.h"
#include "comp-qx.h"
#include "comp-ob.h"
#include "nifty.h"

#include <stdio.h>

#define NSAMP		(8192U)


int
cots_push(cots_ts_t s, cots_tag_t m, cots_to_t t, ...)
{
	static cots_to_t toff[NSAMP];
	static cots_tag_t mtrs[NSAMP];
	static cots_px_t prcs[NSAMP];
	static cots_qx_t qtys[NSAMP];
	static size_t isamp;
	va_list vap;

	mtrs[isamp] = m;
	toff[isamp] = t;
	va_start(vap, t);
	prcs[isamp] = va_arg(vap, cots_px_t);
	qtys[isamp] = va_arg(vap, cots_qx_t);
	va_end(vap);

	if (UNLIKELY(++isamp == countof(toff))) {
		static uint8_t data[sizeof(toff)];
		size_t z;

		z = comp_to(data, toff, countof(toff));
		fprintf(stderr, "toff %zu -> %zu\n", sizeof(toff), z);

		z = comp_tag(data, mtrs, countof(mtrs));
		fprintf(stderr, "mtrs %zu -> %zu\n", sizeof(mtrs), z);

		z = comp_px(data, prcs, countof(prcs));
		fprintf(stderr, "prcs %zu -> %zu\n", sizeof(prcs), z);

		z = comp_qx(data, qtys, countof(qtys));
		fprintf(stderr, "qtys %zu -> %zu\n", sizeof(qtys), z);

		isamp = 0U;
	}
	return 0;
}

/* cotse.c ends here */
