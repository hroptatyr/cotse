/**
   Copyright (C) powturbo 2013-2015
   GPL v2 License
  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

   - homepage : https://sites.google.com/site/powturbo/
   - github   : https://github.com/powturbo
   - twitter  : https://twitter.com/powturbo
   - email    : powturbo [_AT_] gmail [_DOT_] com
**/
#if !defined USIZE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include "bitpack.h"
#include "pfor.h"
#include "nifty.h"

#if defined __INTEL_COMPILER
# pragma warning (disable:2259)
#endif	/* __INTEL_COMPILER */

#define PAD8(__x)	(((__x) + 8 - 1) / 8)
#define P4DSIZE 128 //64 //
#define P4DN   (P4DSIZE/64)

static inline unsigned int
bsr16(uint16_t x)
{
	return x ? 16U - __builtin_clz(x) : 0U;
}

static inline unsigned int
bsr32(uint32_t x)
{
	return x ? 32U - __builtin_clz(x) : 0U;
}

static inline unsigned int
bsr64(uint64_t x)
{
	return x ? 64U - __builtin_clzl(x) : 0U;
}

#if defined __INTEL_COMPILER && defined __SSE4_2__
static inline __attribute__((pure, const)) uint32_t
xpopcnt32(uint32_t v)
{
	return _popcnt32(v);
}


static inline __attribute__((pure, const)) uint32_t
xpopcnt64(uint64_t v)
{
	return _popcnt64(v);
}

#else  /* !__INTEL_COMPILER */
static inline __attribute__((pure, const)) uint32_t
xpopcnt32(uint32_t v)
{
#if 1
	unsigned int c = 0U;
	for (c = 0; v; c++) {
		v &= v - 1;
	}
	return c;
#else
	v = v - ((v >> 1U) & 0x55555555U);
	v = (v & 0x33333333U) + ((v >> 2U) & 0x33333333U);
	/* count */
	return ((v + (v >> 4U) & 0xF0F0F0FU) * 0x1010101U) >> 24U;
#endif	/* 1 */
}

static inline __attribute__((pure, const)) uint32_t
xpopcnt64(uint64_t v)
{
	unsigned int cnt = 0U;

	cnt += xpopcnt32((uint32_t)(v >> 0U));
	cnt += xpopcnt32((uint32_t)(v >> 32U));
	return cnt;
}
#endif	/* __INTEL_COMPILER */

#define P4DEB(__b)		(__b << 1)
#define P4DEBX(__b, __bx)	(__bx << 8 | __b << 1 | 1) 

#define P4DSAVE(__out, __b, __bx)					\
	do {								\
		if (!__bx) {						\
			*__out++ = P4DEB(__b);				\
		} else {						\
			*(unsigned short *)__out = P4DEBX(__b, __bx),	\
				__out += 2;				\
		}							\
	} while(0)

#if defined __SSSE3__
static ALIGNED(char, shuffles[16][16], 16) = {
#define _ 0x80
        { _,_,_,_, _,_,_,_, _,_, _, _,  _, _, _,_  },
        { 0,1,2,3, _,_,_,_, _,_, _, _,  _, _, _,_  },
        { _,_,_,_, 0,1,2,3, _,_, _, _,  _, _, _,_  },
        { 0,1,2,3, 4,5,6,7, _,_, _, _,  _, _, _,_  },
        { _,_,_,_, _,_,_,_, 0,1, 2, 3,  _, _, _,_  },
        { 0,1,2,3, _,_,_,_, 4,5, 6, 7,  _, _, _,_  },
        { _,_,_,_, 0,1,2,3, 4,5, 6, 7,  _, _, _,_  },
        { 0,1,2,3, 4,5,6,7, 8,9,10,11,  _, _, _,_  },
        { _,_,_,_, _,_,_,_, _,_,_,_,    0, 1, 2, 3 },
        { 0,1,2,3, _,_,_,_, _,_,_,  _,  4, 5, 6, 7 },
        { _,_,_,_, 0,1,2,3, _,_,_,  _,  4, 5, 6, 7 },
        { 0,1,2,3, 4,5,6,7, _,_, _, _,  8, 9,10,11 },
        { _,_,_,_, _,_,_,_, 0,1, 2, 3,  4, 5, 6, 7 },
        { 0,1,2,3, _,_,_,_, 4,5, 6, 7,  8, 9,10,11 },
        { _,_,_,_, 0,1,2,3, 4,5, 6, 7,  8, 9,10,11 },
        { 0,1,2,3, 4,5,6,7, 8,9,10,11, 12,13,14,15 }, 
#undef _
};
#endif	/* __SSSE3__ */


#define USIZE		16
#include __FILE__
#undef USIZE

#define USIZE		32
#include __FILE__
#undef USIZE

#define USIZE		64
#include __FILE__
#undef USIZE

#else

#define uint_t paste(paste(uint, USIZE), _t)
#define bsr		paste(bsr, USIZE)
#define _calc		paste(_calc, USIZE)
#define _enc		paste(_enc, USIZE)
#define _dec		paste(_dec, USIZE)
#define bitpack		paste(bitpack, USIZE)
#define bitunpack	paste(bitunpack, USIZE)
#define pfor_enc	paste(pfor_enc, USIZE)
#define pfor_dec	paste(pfor_dec, USIZE)

static unsigned int
_calc(const uint_t *restrict in, size_t n, unsigned int *pbx)
{
	const uint_t *ip;
	uint_t b = 0U;
	int ml, l;
	unsigned int x, bx, cnt[USIZE+1] = {0}; 
  
	for (ip = in; ip != in + (n & ~3);) {
		cnt[bsr(*ip)]++, b |= *ip++;
		cnt[bsr(*ip)]++, b |= *ip++;
		cnt[bsr(*ip)]++, b |= *ip++;
		cnt[bsr(*ip)]++, b |= *ip++;
	}
	while (ip != in + n) {
		cnt[bsr(*ip)]++, b |= *ip++;
	}
	b = bsr(b); 

	bx = b;
	ml = PAD8(n * b) + 1 - 2 - P4DN * 8U;
	x = cnt[b];
	for (int i = b - 1; i >= 0; i--) {
		l = PAD8(n * i) + PAD8(x * (bx - i));
		x += cnt[i];
		if (UNLIKELY(l < ml)) {
			b = i, ml = l;
		}
	}
	*pbx = bx - b;
	return b;
}

static uint8_t*
_enc(uint8_t *restrict out, const uint_t *restrict in, size_t n, unsigned int b, unsigned int bx)
{
	const uint_t msk = (1ULL << b) - 1U;
	uint_t _in[P4DSIZE], inx[P4DSIZE * 2U];
	uint64_t xmap[P4DN];
	unsigned int miss[P4DSIZE];
	size_t i;
	size_t xn;

	memset(xmap, 0, sizeof(xmap));

	for (xn = i = 0U; i != (n & ~3ULL); i += 4U) {
		miss[xn] = i + 0U, xn += in[i + 0U] > msk,
			_in[i + 0U] = in[i + 0U] & msk;
		miss[xn] = i + 1U, xn += in[i + 1U] > msk,
			_in[i + 1U] = in[i + 1U] & msk;
		miss[xn] = i + 2U, xn += in[i + 2U] > msk,
			_in[i + 2U] = in[i + 2U] & msk;
		miss[xn] = i + 3U, xn += in[i + 3U] > msk,
			_in[i + 3U] = in[i + 3U] & msk;
	}
	for (; i != n; i++) {
		miss[xn] = i, xn += in[i] > msk, _in[i] = in[i] & msk;
	}
	for (i = 0U; i != xn; i++) {
		unsigned int c = miss[i];

		xmap[c >> 6U] |= (1ULL << (c & 0x3f));
		inx[i] = in[c] >> b;
	}
	out += bitpack(out, _in,  n,  b);
	*(unsigned long long*)out = xmap[0U], out += sizeof(*xmap);
	*(unsigned long long*)out = xmap[1U], out += sizeof(*xmap);
	out += bitpack(out, inx, xn, bx);
	return out;
}


static const uint8_t*
_dec(uint_t *restrict out, size_t n, const uint8_t *restrict in, unsigned int b, unsigned int bx)
{
	uint_t ex[0x100U + 8U];
	uint64_t bb[P4DN];
  
	in += bitunpack(out, in, n, b >> 1U);

	if (!(b & 0b1)) {
		return in;
	}
	b >>= 1U;

	with (unsigned int num = 0U) {
		bb[0U] = ((const uint64_t*)in)[0U];
		bb[1U] = ((const uint64_t*)in)[1U];
		num += xpopcnt64(bb[0U]);
		num += xpopcnt64(bb[1U]);
		in += 2U * sizeof(*bb);
		in += bitunpack(ex, in, num, bx);
	}

#if defined __SSSE3__ && USIZE == 32
	uint_t *op, *pex = ex;

	for (op = out; bb[0U]; bb[0U] >>= 4U, op += 4U) {
		const unsigned int m = bb[0U] & 0xfU;
		register __m128i rop = _mm_loadu_si128((__m128i*)op);
		register __m128i rpex = _mm_loadu_si128((__m128i*)pex);
		register __m128i x = _mm_slli_epi32(rpex, b);
		register __m128i rsh = _mm_load_si128((__m128i*)shuffles[m]);

		x = _mm_shuffle_epi8(x, rsh);
		x = _mm_add_epi32(rop, x);
		_mm_storeu_si128((__m128i*)op, x);
		pex += xpopcnt32(m);
	}
	for (op = out; bb[1U]; bb[1U] >>= 4U, op += 4U) {
		const unsigned int m = bb[0U] & 0xfU;
		register __m128i rop = _mm_loadu_si128((__m128i*)op);
		register __m128i rpex = _mm_loadu_si128((__m128i*)pex);
		register __m128i x = _mm_slli_epi32(rpex, b);
		register __m128i rsh = _mm_load_si128((__m128i*)shuffles[m]);

		x = _mm_shuffle_epi8(x, rsh);
		x = _mm_add_epi32(rop, x);
		_mm_storeu_si128((__m128i*)op, x);
		pex += xpopcnt32(m);
	}

#else
	with (size_t k = 0U) {
		while (bb[0U]) {
			unsigned int x = __builtin_ctzll(bb[0U]);
			out[x] += ex[k++] << b;
			bb[0U] ^= (1ULL << x);
		}

		out += 64U;
		while (bb[1U]) {
			unsigned int x = __builtin_ctzll(bb[1U]);
			out[x] += ex[k++] << b;
			bb[1U] ^= (1ULL << x);
		}
	}
#endif	/* !__SSSE3__ */
	return in;
}


/* public API */
size_t
pfor_enc(uint8_t *restrict out, const uint_t *restrict in, size_t n)
{
	const uint8_t *const oout = out;

	for (size_t i = 0U; i < n; i += P4DSIZE) {
		const size_t z = P4DSIZE < n - i ? P4DSIZE : n - i;
		unsigned int bx;
		unsigned int b = _calc(in + i, z, &bx);

		P4DSAVE(out, b, bx);
		if (UNLIKELY(!bx || b == USIZE)) {
			out += bitpack(out, in + i, z, b);
			continue;
		}
		out = _enc(out, in + i, z, b, bx);
	}
	return out - oout;
}

size_t
pfor_dec(uint_t *restrict out, const uint8_t *restrict in, size_t n)
{
	const uint8_t *const oin = in;

	for (size_t i = 0U; i < n; i += P4DSIZE) {
		const size_t nt = P4DSIZE < n - i ? P4DSIZE : n - i;
		unsigned int b = *in++, bx = -1U;

		if (b & 0b1U) {
			bx = *in++;
		}
		in = _dec(out + i, nt, in, b, bx);
	}
	return in - oin;
}

#undef bsr
#undef _calc
#undef _enc
#undef _dec
#undef pfor_enc
#undef pfor_dec
#undef bitpack
#undef bitunpack

#endif
