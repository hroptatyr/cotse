/*** intern.c -- interning system
 *
 * Copyright (C) 2013-2016 Sebastian Freundt
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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "intern.h"
#include "comp-ob.h"
#include "hash.h"
#include "nifty.h"

/* number of objects kept spare at any time */
#define NOBS_MIN	(16U)
/* minimum number of slots in the hash table */
#define ZOBS_MIN	(64U)

struct cots_ob_s {
	size_t nobs;

	/* beef table */
	size_t ztbl;
	struct {
		cots_tag_t mc;
		cots_hx_t hx;
	} *tbl;

	/* map into strings */
	size_t *off;

	/* interned strings */
	size_t zobs;
	uint8_t *obs;
};


static size_t
_next_2pow(size_t z)
{
	z--;
	z |= z >> 1U;
	z |= z >> 2U;
	z |= z >> 4U;
	z |= z >> 8U;
	z |= z >> 16U;
	z |= z >> 32U;
	z++;
	return z;
}

static cots_tag_t
make_obint(cots_ob_t ob, const char *str, size_t len)
{
/* put STR (of length LEN) into string obarray, don't check for dups */
	size_t off = ob->off[ob->nobs];

	if (UNLIKELY(off + len + 1U/*\nul*/ + 1U/*len*/ > ob->zobs)) {
		const size_t nuz = ob->zobs * 2U;

		ob->obs = realloc(ob->obs, nuz * sizeof(*ob->obs));
		if (UNLIKELY(ob->obs == NULL)) {
			ob->zobs = 0U;
			return 0U;
		}
		ob->zobs = nuz;
	}
	/* paste the string in question */
	memcpy(ob->obs + off, str, len);
	/* point to after the string */
	off += len;
	/* nul terminate */
	ob->obs[off++] = '\0';

	/* advance the number of strings in the array */
	ob->nobs++;
	if (UNLIKELY(!(ob->nobs % NOBS_MIN))) {
		/* resize offset array */
		size_t nuz = ob->nobs + NOBS_MIN;

		ob->off = realloc(ob->off, nuz * sizeof(*ob->off));
		if (UNLIKELY(ob->off == NULL)) {
			return 0U;
		}
	}
	/* track the end of the obs array */
	ob->off[ob->nobs] = off;
	return ob->nobs;
}

static int
resz_tbl(cots_ob_t ob)
{
	size_t nuz = (ob->ztbl + 1U) * 2U;
	typeof(ob->tbl) nup = calloc(nuz, sizeof(*nup));

	if (UNLIKELY(nup == NULL)) {
		return -1;
	}

	/* start rehashing then */
	nuz--;
	for (size_t i = 0U; i <= ob->ztbl; i++) {
		const cots_hx_t slhx = ob->tbl[i].hx;

		if (!slhx) {
			continue;
		}

		/* calc new slot */
		with (size_t j = slhx & nuz) {
			nup[j] = ob->tbl[i];
		}
	}
	/* free the old guy */
	free(ob->tbl);
	/* yay, make the thing larger now, for real */
	ob->tbl = nup;
	ob->ztbl = nuz;
	return 0;
}


/* public api */
cots_ob_t
make_cots_ob(void)
{
	cots_ob_t res = calloc(1, sizeof(struct cots_ob_s));

	if (UNLIKELY(res == NULL)) {
		goto err_res;
	}
	/* otherwise try to provision some space */
	res->off = calloc(NOBS_MIN, sizeof(*res->off));
	if (UNLIKELY(res->off == NULL)) {
		goto err_off;
	}

	res->tbl = calloc(NOBS_MIN, sizeof(*res->tbl));
	if (UNLIKELY(res->tbl == NULL)) {
		goto err_tbl;
	}
	/* to have a bitmask we track the size 1 smaller than it is */
	res->ztbl = NOBS_MIN - 1U;

	res->obs = malloc(ZOBS_MIN);
	if (UNLIKELY(res->obs == NULL)) {
		goto err_obs;
	}
	res->zobs = ZOBS_MIN;
	return res;

err_obs:
	free(res->tbl);
err_tbl:
	free(res->off);
err_off:
	free(res);
err_res:
	return NULL;
}

void
free_cots_ob(cots_ob_t ob)
{
	if (LIKELY(ob->tbl != NULL)) {
		free(ob->tbl);
	}
	if (LIKELY(ob->off != NULL)) {
		free(ob->off);
	}
	if (LIKELY(ob->obs != NULL)) {
		free(ob->obs);
	}
	free(ob);
	return;
}

cots_tag_t
cots_intern(cots_ob_t ob, const char *str, size_t len)
{
	if (UNLIKELY(!len)) {
		/* don't bother */
		return 0U;
	}

	/* get us a hash */
	const cots_hx_t hx = hash((const uint8_t*)str, len);

	for (size_t slot = hx & ob->ztbl;;) {
		const cots_hx_t slhx = ob->tbl[slot].hx;

		if (UNLIKELY(!slhx)) {
			/* found empty slot */
			const cots_tag_t mc = make_obint(ob, str, len);
			ob->tbl[slot].mc = mc;
			ob->tbl[slot].hx = hx;
		} else if (UNLIKELY(slhx != hx)) {
			/* collision, do some resizing then */
			if (UNLIKELY(resz_tbl(ob) < 0)) {
				break;
			}
			continue;
		}

		return ob->tbl[slot].mc;
	}
	return 0U;
}

const char*
cots_tag_name(cots_ob_t ob, cots_tag_t m)
{
	if (UNLIKELY(!m || ob->nobs < m)) {
		return NULL;
	}
	return (char*)ob->obs + ob->off[--m];
}


/* comp-ob API */
/* compress */
size_t
comp_ob(uint8_t *restrict tgt, const struct cots_ob_s *restrict ob)
{
	const size_t off = ob->off[ob->nobs];
	size_t res = 0U;

	memcpy(tgt, &off, sizeof(off));
	res += sizeof(off);

	memcpy(tgt + res, ob->obs, off);
	res += off;
	return res;
}

/* decompress */
cots_ob_t
dcmp_ob(const uint8_t *restrict c, size_t nz)
{
	struct cots_ob_s res = {0UL}, *rp;
	size_t ci = 0U;
	size_t tot;

	memcpy(&tot, c, sizeof(tot));
	ci += sizeof(tot);
	if (UNLIKELY(tot + ci > nz)) {
		return NULL;
	}

	/* count the number of \nul's to determine NOBS */
	for (const uint8_t *cp = c + ci, *const ep = cp + tot; cp < ep; cp++) {
		if (UNLIKELY((cp = memchr(cp, '\0', ep - cp)) == NULL)) {
			break;
		}
		res.nobs++;
	}
	if (UNLIKELY(!res.nobs)) {
		return NULL;
	}
	/* build off array */
	with (const size_t zoff = (((res.nobs / NOBS_MIN) + 1U) * NOBS_MIN)) {
		const char *const obs = (const char*)(c + ci);

		res.off = malloc(zoff * sizeof(*res.off));
		if (UNLIKELY(res.off == NULL)) {
			return NULL;
		}
		/* determine offsets by cumsum'ming the strlen's */
		res.off[0U] = 0U;
		for (size_t i = 1U; i <= res.nobs; i++) {
			const size_t off = res.off[i - 1U];
			const size_t len = strlen(obs + off) + 1U;
			res.off[i] = len + off;
		}
	}
	/* round up res.ztbl to next 2 power */
	res.ztbl = _next_2pow(res.nobs);
	/* rough estimate */
	res.ztbl = res.ztbl > NOBS_MIN ? res.ztbl : NOBS_MIN;
	res.tbl = calloc(res.ztbl, sizeof(*res.tbl));
	res.ztbl--;

	/* now hash them all */
	for (size_t i = 0U; i < res.nobs; i++) {
		const uint8_t *const str = c + ci + res.off[i];
		const size_t len = res.off[i + 1U] - res.off[i];
		const cots_hx_t hx = hash(str, len);

		for (size_t slot = hx & res.ztbl;;) {
			const cots_hx_t slhx = res.tbl[slot].hx;

			if (UNLIKELY(!slhx)) {
				/* found empty slot */
				res.tbl[slot].mc = i + 1U;
				res.tbl[slot].hx = hx;
			} else if (UNLIKELY(slhx != hx)) {
				/* collision, do some resizing then */
				if (UNLIKELY(resz_tbl(&res) < 0)) {
					goto err_rsz;
				}
				continue;
			}
			break;
		}
	}

	/* looking brill, copy the string beef */
	res.zobs = _next_2pow(tot);
	res.obs = malloc(res.zobs);
	if (UNLIKELY(res.obs == NULL)) {
		goto err_obs;
	}
	memcpy(res.obs, c + ci, tot);

	/* and now the container */
	rp = malloc(sizeof(res));
	if (UNLIKELY(rp == NULL)) {
		goto err_ob;
	}
	*rp = res;
	return rp;

err_ob:
	free(res.obs);
err_obs:
	free(res.tbl);
err_rsz:
	free(res.off);
	return NULL;
}

/* intern.c ends here */
