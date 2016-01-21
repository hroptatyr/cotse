/*** index.c -- indexing
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
#include <string.h>
#include <fcntl.h>
#include "cotse.h"
#include "index.h"
#include "nifty.h"

struct idxt_s {
	struct cots_tick_s proto;
	uint64_t off;
	uint64_t cnt;
};


cots_idx_t
make_cots_idx(const char *filename)
{
	cots_ss_t res;

	if (UNLIKELY(filename == NULL)) {
		goto nul_out;
	} else if (UNLIKELY((res = make_cots_ss("cz", 512U)) == NULL)) {
		goto nul_out;
	}
	/* construct temp filename */
	with (size_t z = strlen(filename)) {
		char idxfn[z + 5U];
		const int idxfl = O_CREAT | O_TRUNC/*?*/ | O_RDWR;

		memcpy(idxfn, filename, z);
		memcpy(idxfn + z, ".idx", sizeof(".idx"));
		if (UNLIKELY(cots_attach(res, idxfn, idxfl) < 0)) {
			goto fre_out;
		}
	}
	return res;

fre_out:
	free_cots_ss(res);
nul_out:
	return NULL;
}

void
free_cots_idx(cots_idx_t s)
{
	free_cots_ss(s);
	return;
}

int
cots_add_index(cots_idx_t s, struct trng_s tr, struct orng_s or, size_t nt)
{
	int rc = 0;

	rc += cots_bang_tick(s, &(struct idxt_s){{tr.from}, or.beg, nt}.proto);
	rc += cots_keep_last(s);
	rc += cots_bang_tick(s, &(struct idxt_s){{tr.till}, or.end, nt}.proto);
	return rc;
}

/* index.c ends here */
