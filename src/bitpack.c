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
#if !defined IPPB
#include <stdio.h>
#include "bitpack.h"
 
#if defined __INTEL_COMPILER
# pragma warning (disable:177)
# pragma warning (disable:2259)
# pragma warning (disable:2338)
#endif	/* __INTEL_COMPILER */

#define PAD8(__x)	(((__x) + 8U - 1U) / 8U)

#define IPPB( __ip,__x, __parm)
#define SRC( __ip,__x)	(*__ip++)
#define SRC1(__ip,__x)	(*(__ip))
#define DSTI(__op)
#define BPI(__w, __x, __parm)	__w
#include __FILE__
 
size_t
bitpack16(uint8_t *restrict out, const uint16_t *restrict in, size_t n, unsigned int nbits)
{
	BITPACK32(in, n, nbits, out, 0);
	return PAD8(n * nbits);
}

size_t
bitpack32(uint8_t *restrict out, const uint32_t *restrict in, size_t n, unsigned int nbits)
{
	BITPACK32(in, n, nbits, out, 0);
	return PAD8(n * nbits);
}

size_t
bitpack64(uint8_t *restrict out, const uint64_t *restrict in, size_t n, unsigned int nbits)
{
	BITPACK64(in, n, nbits, out, 0);
	return PAD8(n * nbits);
}

size_t
bitunpack16(uint16_t *restrict out, const uint8_t *restrict in, size_t n, unsigned int nbits)
{
	BITUNPACK32(in, n, nbits, out, 0);
	return PAD8(n * nbits);
}

size_t
bitunpack32(uint32_t *restrict out, const uint8_t *restrict in, size_t n, unsigned int nbits)
{
	BITUNPACK32(in, n, nbits, out, 0);
	return PAD8(n * nbits);
}

size_t
bitunpack64(uint64_t *restrict out, const uint8_t *restrict in, size_t n, unsigned int nbits)
{
	BITUNPACK64(in, n, nbits, out, 0);
	return PAD8(n * nbits);
}

#undef IPPB 
#undef SRC
#undef SRC1
#undef BPI
#undef DSTI


#else  /* IPPB */

#include <stdint.h>
#include "bitpack64.h"
#define SRCI(__ip)

#define BITPACK32(__ip, __n, __nbits, __op, __parm) {			\
  const typeof(__ip[0]) *_ipe=(__ip)+(__n);/*((__n+31)&0xffffffe0u)*/;  \
  switch(__nbits) {\
    case  0:__ip = _ipe; break;\
    case  1:do BITPACK64_1( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  2:do BITPACK64_2( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  3:do BITPACK64_3( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  4:do BITPACK64_4( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  5:do BITPACK64_5( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  6:do BITPACK64_6( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  7:do BITPACK64_7( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  8:do BITPACK64_8( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  9:do BITPACK64_9( __ip, __op, __parm) while(__ip < _ipe); break;\
    case 10:do BITPACK64_10(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 11:do BITPACK64_11(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 12:do BITPACK64_12(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 13:do BITPACK64_13(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 14:do BITPACK64_14(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 15:do BITPACK64_15(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 16:do BITPACK64_16(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 17:do BITPACK64_17(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 18:do BITPACK64_18(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 19:do BITPACK64_19(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 20:do BITPACK64_20(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 21:do BITPACK64_21(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 22:do BITPACK64_22(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 23:do BITPACK64_23(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 24:do BITPACK64_24(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 25:do BITPACK64_25(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 26:do BITPACK64_26(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 27:do BITPACK64_27(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 28:do BITPACK64_28(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 29:do BITPACK64_29(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 30:do BITPACK64_30(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 31:do BITPACK64_31(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 32:do BITPACK64_32(__ip, __op, __parm) while(__ip < _ipe);\
  }\
}

#define BITPACK64(__ip, __n, __nbits, __op, __parm) {		       \
  const typeof(__ip[0]) *_ipe=(__ip)+(__n);/*((__n+31)&0xffffffe0u)*/; \
  switch(__nbits) {\
    case  0:__ip = _ipe; break;\
    case  1:do BITPACK64_1( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  2:do BITPACK64_2( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  3:do BITPACK64_3( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  4:do BITPACK64_4( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  5:do BITPACK64_5( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  6:do BITPACK64_6( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  7:do BITPACK64_7( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  8:do BITPACK64_8( __ip, __op, __parm) while(__ip < _ipe); break;\
    case  9:do BITPACK64_9( __ip, __op, __parm) while(__ip < _ipe); break;\
    case 10:do BITPACK64_10(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 11:do BITPACK64_11(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 12:do BITPACK64_12(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 13:do BITPACK64_13(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 14:do BITPACK64_14(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 15:do BITPACK64_15(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 16:do BITPACK64_16(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 17:do BITPACK64_17(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 18:do BITPACK64_18(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 19:do BITPACK64_19(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 20:do BITPACK64_20(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 21:do BITPACK64_21(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 22:do BITPACK64_22(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 23:do BITPACK64_23(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 24:do BITPACK64_24(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 25:do BITPACK64_25(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 26:do BITPACK64_26(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 27:do BITPACK64_27(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 28:do BITPACK64_28(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 29:do BITPACK64_29(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 30:do BITPACK64_30(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 31:do BITPACK64_31(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 32:do BITPACK64_32(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 33:do BITPACK64_33(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 34:do BITPACK64_34(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 35:do BITPACK64_35(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 36:do BITPACK64_36(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 37:do BITPACK64_37(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 38:do BITPACK64_38(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 39:do BITPACK64_39(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 40:do BITPACK64_40(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 41:do BITPACK64_41(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 42:do BITPACK64_42(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 43:do BITPACK64_43(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 44:do BITPACK64_44(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 45:do BITPACK64_45(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 46:do BITPACK64_46(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 47:do BITPACK64_47(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 48:do BITPACK64_48(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 49:do BITPACK64_49(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 50:do BITPACK64_50(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 51:do BITPACK64_51(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 52:do BITPACK64_52(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 53:do BITPACK64_53(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 54:do BITPACK64_54(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 55:do BITPACK64_55(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 56:do BITPACK64_56(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 57:do BITPACK64_57(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 58:do BITPACK64_58(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 59:do BITPACK64_59(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 60:do BITPACK64_60(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 61:do BITPACK64_61(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 62:do BITPACK64_62(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 63:do BITPACK64_63(__ip, __op, __parm) while(__ip < _ipe); break;\
    case 64:do BITPACK64_64(__ip, __op, __parm) while(__ip < _ipe);\
  }\
}


#define DST( __op, __x, __w, __parm)	*__op++ = BPI(__w, __x, __parm)

#define BITUNPACK32(__ip, __n, __nbits, __op, __parm) { typeof(__op[0]) *__ope = __op + __n,*_op=__op;\
  switch(__nbits) {\
    case  0: do BITUNPACK64_0( __ip, __op, __parm) while(__op<__ope); break;\
    case  1: do BITUNPACK64_1( __ip, __op, __parm) while(__op<__ope); break;\
    case  2: do BITUNPACK64_2( __ip, __op, __parm) while(__op<__ope); break;\
    case  3: do BITUNPACK64_3( __ip, __op, __parm) while(__op<__ope); break;\
    case  4: do BITUNPACK64_4( __ip, __op, __parm) while(__op<__ope); break;\
    case  5: do BITUNPACK64_5( __ip, __op, __parm) while(__op<__ope); break;\
    case  6: do BITUNPACK64_6( __ip, __op, __parm) while(__op<__ope); break;\
    case  7: do BITUNPACK64_7( __ip, __op, __parm) while(__op<__ope); break;\
    case  8: do BITUNPACK64_8( __ip, __op, __parm) while(__op<__ope); break;\
    case  9: do BITUNPACK64_9( __ip, __op, __parm) while(__op<__ope); break;\
    case 10: do BITUNPACK64_10(__ip, __op, __parm) while(__op<__ope); break;\
    case 11: do BITUNPACK64_11(__ip, __op, __parm) while(__op<__ope); break;\
    case 12: do BITUNPACK64_12(__ip, __op, __parm) while(__op<__ope); break;\
    case 13: do BITUNPACK64_13(__ip, __op, __parm) while(__op<__ope); break;\
    case 14: do BITUNPACK64_14(__ip, __op, __parm) while(__op<__ope); break;\
    case 15: do BITUNPACK64_15(__ip, __op, __parm) while(__op<__ope); break;\
    case 16: do BITUNPACK64_16(__ip, __op, __parm) while(__op<__ope); break;\
    case 17: do BITUNPACK64_17(__ip, __op, __parm) while(__op<__ope); break;\
    case 18: do BITUNPACK64_18(__ip, __op, __parm) while(__op<__ope); break;\
    case 19: do BITUNPACK64_19(__ip, __op, __parm) while(__op<__ope); break;\
    case 20: do BITUNPACK64_20(__ip, __op, __parm) while(__op<__ope); break;\
    case 21: do BITUNPACK64_21(__ip, __op, __parm) while(__op<__ope); break;\
    case 22: do BITUNPACK64_22(__ip, __op, __parm) while(__op<__ope); break;\
    case 23: do BITUNPACK64_23(__ip, __op, __parm) while(__op<__ope); break;\
    case 24: do BITUNPACK64_24(__ip, __op, __parm) while(__op<__ope); break;\
    case 25: do BITUNPACK64_25(__ip, __op, __parm) while(__op<__ope); break;\
    case 26: do BITUNPACK64_26(__ip, __op, __parm) while(__op<__ope); break;\
    case 27: do BITUNPACK64_27(__ip, __op, __parm) while(__op<__ope); break;\
    case 28: do BITUNPACK64_28(__ip, __op, __parm) while(__op<__ope); break;\
    case 29: do BITUNPACK64_29(__ip, __op, __parm) while(__op<__ope); break;\
    case 30: do BITUNPACK64_30(__ip, __op, __parm) while(__op<__ope); break;\
    case 31: do BITUNPACK64_31(__ip, __op, __parm) while(__op<__ope); break;\
    case 32: do BITUNPACK64_32(__ip, __op, __parm) while(__op<__ope); break;\
  }\
}

#define BITUNPACK64(__ip, __n, __nbits, __op, __parm) { typeof(__op[0]) *__ope = __op + __n,*_op=__op;\
  switch(__nbits) {\
    case  0: do BITUNPACK64_0( __ip, __op, __parm) while(__op<__ope); break;\
    case  1: do BITUNPACK64_1( __ip, __op, __parm) while(__op<__ope); break;\
    case  2: do BITUNPACK64_2( __ip, __op, __parm) while(__op<__ope); break;\
    case  3: do BITUNPACK64_3( __ip, __op, __parm) while(__op<__ope); break;\
    case  4: do BITUNPACK64_4( __ip, __op, __parm) while(__op<__ope); break;\
    case  5: do BITUNPACK64_5( __ip, __op, __parm) while(__op<__ope); break;\
    case  6: do BITUNPACK64_6( __ip, __op, __parm) while(__op<__ope); break;\
    case  7: do BITUNPACK64_7( __ip, __op, __parm) while(__op<__ope); break;\
    case  8: do BITUNPACK64_8( __ip, __op, __parm) while(__op<__ope); break;\
    case  9: do BITUNPACK64_9( __ip, __op, __parm) while(__op<__ope); break;\
    case 10: do BITUNPACK64_10(__ip, __op, __parm) while(__op<__ope); break;\
    case 11: do BITUNPACK64_11(__ip, __op, __parm) while(__op<__ope); break;\
    case 12: do BITUNPACK64_12(__ip, __op, __parm) while(__op<__ope); break;\
    case 13: do BITUNPACK64_13(__ip, __op, __parm) while(__op<__ope); break;\
    case 14: do BITUNPACK64_14(__ip, __op, __parm) while(__op<__ope); break;\
    case 15: do BITUNPACK64_15(__ip, __op, __parm) while(__op<__ope); break;\
    case 16: do BITUNPACK64_16(__ip, __op, __parm) while(__op<__ope); break;\
    case 17: do BITUNPACK64_17(__ip, __op, __parm) while(__op<__ope); break;\
    case 18: do BITUNPACK64_18(__ip, __op, __parm) while(__op<__ope); break;\
    case 19: do BITUNPACK64_19(__ip, __op, __parm) while(__op<__ope); break;\
    case 20: do BITUNPACK64_20(__ip, __op, __parm) while(__op<__ope); break;\
    case 21: do BITUNPACK64_21(__ip, __op, __parm) while(__op<__ope); break;\
    case 22: do BITUNPACK64_22(__ip, __op, __parm) while(__op<__ope); break;\
    case 23: do BITUNPACK64_23(__ip, __op, __parm) while(__op<__ope); break;\
    case 24: do BITUNPACK64_24(__ip, __op, __parm) while(__op<__ope); break;\
    case 25: do BITUNPACK64_25(__ip, __op, __parm) while(__op<__ope); break;\
    case 26: do BITUNPACK64_26(__ip, __op, __parm) while(__op<__ope); break;\
    case 27: do BITUNPACK64_27(__ip, __op, __parm) while(__op<__ope); break;\
    case 28: do BITUNPACK64_28(__ip, __op, __parm) while(__op<__ope); break;\
    case 29: do BITUNPACK64_29(__ip, __op, __parm) while(__op<__ope); break;\
    case 30: do BITUNPACK64_30(__ip, __op, __parm) while(__op<__ope); break;\
    case 31: do BITUNPACK64_31(__ip, __op, __parm) while(__op<__ope); break;\
    case 32: do BITUNPACK64_32(__ip, __op, __parm) while(__op<__ope); break;\
    case 33: do BITUNPACK64_33(__ip, __op, __parm) while(__op<__ope); break;\
    case 34: do BITUNPACK64_34(__ip, __op, __parm) while(__op<__ope); break;\
    case 35: do BITUNPACK64_35(__ip, __op, __parm) while(__op<__ope); break;\
    case 36: do BITUNPACK64_36(__ip, __op, __parm) while(__op<__ope); break;\
    case 37: do BITUNPACK64_37(__ip, __op, __parm) while(__op<__ope); break;\
    case 38: do BITUNPACK64_38(__ip, __op, __parm) while(__op<__ope); break;\
    case 39: do BITUNPACK64_39(__ip, __op, __parm) while(__op<__ope); break;\
    case 40: do BITUNPACK64_40(__ip, __op, __parm) while(__op<__ope); break;\
    case 41: do BITUNPACK64_41(__ip, __op, __parm) while(__op<__ope); break;\
    case 42: do BITUNPACK64_42(__ip, __op, __parm) while(__op<__ope); break;\
    case 43: do BITUNPACK64_43(__ip, __op, __parm) while(__op<__ope); break;\
    case 44: do BITUNPACK64_44(__ip, __op, __parm) while(__op<__ope); break;\
    case 45: do BITUNPACK64_45(__ip, __op, __parm) while(__op<__ope); break;\
    case 46: do BITUNPACK64_46(__ip, __op, __parm) while(__op<__ope); break;\
    case 47: do BITUNPACK64_47(__ip, __op, __parm) while(__op<__ope); break;\
    case 48: do BITUNPACK64_48(__ip, __op, __parm) while(__op<__ope); break;\
    case 49: do BITUNPACK64_49(__ip, __op, __parm) while(__op<__ope); break;\
    case 50: do BITUNPACK64_50(__ip, __op, __parm) while(__op<__ope); break;\
    case 51: do BITUNPACK64_51(__ip, __op, __parm) while(__op<__ope); break;\
    case 52: do BITUNPACK64_52(__ip, __op, __parm) while(__op<__ope); break;\
    case 53: do BITUNPACK64_53(__ip, __op, __parm) while(__op<__ope); break;\
    case 54: do BITUNPACK64_54(__ip, __op, __parm) while(__op<__ope); break;\
    case 55: do BITUNPACK64_55(__ip, __op, __parm) while(__op<__ope); break;\
    case 56: do BITUNPACK64_56(__ip, __op, __parm) while(__op<__ope); break;\
    case 57: do BITUNPACK64_57(__ip, __op, __parm) while(__op<__ope); break;\
    case 58: do BITUNPACK64_58(__ip, __op, __parm) while(__op<__ope); break;\
    case 59: do BITUNPACK64_59(__ip, __op, __parm) while(__op<__ope); break;\
    case 60: do BITUNPACK64_60(__ip, __op, __parm) while(__op<__ope); break;\
    case 61: do BITUNPACK64_61(__ip, __op, __parm) while(__op<__ope); break;\
    case 62: do BITUNPACK64_62(__ip, __op, __parm) while(__op<__ope); break;\
    case 63: do BITUNPACK64_63(__ip, __op, __parm) while(__op<__ope); break;\
    case 64: do BITUNPACK64_64(__ip, __op, __parm) while(__op<__ope); break;\
  }\
}

#endif
