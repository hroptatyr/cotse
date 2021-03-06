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
#if !defined INCLUDED_pfor_h_
#define INCLUDED_pfor_h_
#include <stdint.h>
#include <stdlib.h>

// compress integer array with n values to the buffer out. Return value = end of compressed buffer out
extern size_t
pfor_enc16(uint8_t *restrict out, const uint16_t *restrict in, size_t n);
extern size_t
pfor_enc32(uint8_t *restrict out, const uint32_t *restrict in, size_t n);
extern size_t
pfor_enc64(uint8_t *restrict out, const uint64_t *restrict in, size_t n);

// decompress a previously (with p4denc32) 32 bits packed array. Return value = end of packed buffer in 
extern size_t
pfor_dec16(uint16_t *restrict out, const uint8_t *restrict in, size_t n);
extern size_t
pfor_dec32(uint32_t *restrict out, const uint8_t *restrict in, size_t n);
extern size_t
pfor_dec64(uint64_t *restrict out, const uint8_t *restrict in, size_t n);

#endif	/* INCLUDED_pfor_h_ */
