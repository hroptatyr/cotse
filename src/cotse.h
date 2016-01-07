/*** cotse.h -- cotse API
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
#if !defined INCLUDED_cotse_h_
#define INCLUDED_cotse_h_
#include <stdint.h>

/**
 * Time offset with respect to a point on the timeline (cots_tm_t).
 * Measured in nanoseconds. */
typedef uint64_t cots_to_t;

/**
 * Time, a point on the timeline. */
typedef struct {
	uint64_t epoch;
} cots_tm_t;

/**
 * Hash value. */
typedef uint64_t cots_hx_t;

/**
 * Price value. */
typedef _Decimal32 cots_px_t;

/**
 * Quantity value. */
typedef _Decimal64 cots_qx_t;

/**
 * Time series. */
typedef struct {
	/** reference time */
	cots_tm_t reftm;
} cots_ts_t;


/* public API */
/**
 * Create a time series object. */
extern cots_ts_t make_ts(void);

/**
 * Free a time series object. */
extern void free_ts(cots_ts_t);

#endif	/* INCLUDED_cotse_h_ */
