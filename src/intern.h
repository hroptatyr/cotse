/*** intern.h -- interning system
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
#if !defined INCLUDED_intern_h_
#define INCLUDED_intern_h_
#include <stdint.h>
#include "cotse.h"

/**
 * Obarray. */
typedef struct cots_ob_s *cots_ob_t;

/**
 * Return the interned representation of STR of length LEN in OB. */
extern cots_tag_t cots_intern(cots_ob_t ob, const char *str, size_t len);

/**
 * Unintern the metric M. */
extern void cots_unintern(cots_ob_t ob, cots_tag_t m);

/**
 * Return the string representation of a metric code. */
extern const char *cots_tag_name(cots_ob_t, cots_tag_t);

/**
 * Create an obarray. */
extern cots_ob_t make_cots_ob(void);

/**
 * Free an obarray. */
extern void free_cots_ob(cots_ob_t);


/* serialiser */
/**
 * Prepare obarray OB for serialising, return size in bytes. */
extern size_t
wr_ob(uint8_t **const tgt, const struct cots_ob_s *restrict ob);

/**
 * Deserialise obarray in C of size NZ (bytes). */
extern cots_ob_t
rd_ob(const uint8_t *restrict c, size_t nz);

#endif	/* INCLUDED_intern_h_ */
