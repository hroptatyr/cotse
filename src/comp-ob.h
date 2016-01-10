/*** comp-ob.h -- compression routines for obarrays
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
#if !defined INCLUDED_comp_ob_h_
#define INCLUDED_comp_ob_h_
#include <stdint.h>
#include <stdlib.h>
#include "cotse.h"
#include "intern.h"


/**
 * Compress NM metric codes M into TGT, return the number of bytes. */
extern size_t
comp_tag(uint8_t *restrict tgt, const cots_tag_t *restrict m, size_t nm);

/**
 * Decompress NZ bytes in C into metric codes, return number of codes. */
extern size_t
dcmp_tag(cots_tag_t *restrict tgt, const uint8_t *restrict c, size_t nz);

/**
 * Compress obarray OB into TGT, return the number of bytes. */
extern size_t
comp_ob(uint8_t *restrict tgt, const struct cots_ob_s *restrict ob);

/**
 * Decompress NZ bytes in C into obarray, return obarray object. */
extern cots_ob_t
dcmp_ob(const uint8_t *restrict c, size_t nz);

#endif	/* INCLUDED_comp_ob_h_ */
