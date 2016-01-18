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
 * Tag code. */
typedef uint64_t cots_tag_t;

/**
 * Price value. */
typedef _Decimal32 cots_px_t;

/**
 * Quantity value. */
typedef _Decimal64 cots_qx_t;

/**
 * Special value (-0) to denote missing px data.
 * Using this instead of NaN will ensure good compaction. */
#define COTS_PX_MISS						\
	(union {uint32_t u32; _Decimal32 d32;}){0x80000000U}

/**
 * Special value (-0) to denote missing px data.
 * Using this instead of NaN will ensure good compaction. */
#define COTS_QX_MISS							\
	(union {uint64_t u64; _Decimal64 d64;}){0x8000000000000000ULL}


/**
 * Time series. */
typedef struct cots_ts_s {
	/** reference time */
	const cots_tm_t reftm;
	/** number of fields */
	const size_t nfields;
	/** block size (number of ticks per page) */
	const size_t blockz;
	/** layout (for reference), nul-terminated
	 * Each character represents one field type,
	 * see COTS_LO_* definitions for details.
	 * Unsupported field types will be ignored. */
	const char *layout;
	/** Field names for documentation purposes, NULL terminated. */
	const char *const *fields;
	/** Currently attached file, if any. */
	const char *filename;
} *cots_ts_t;

/* layout values */
#define COTS_LO_END	'\0'
#define COTS_LO_BYT	'b'
#define COTS_LO_TIM	't'
#define COTS_LO_TAG	'm'
#define COTS_LO_PRC	'p'
#define COTS_LO_QTY	'q'
#define COTS_LO_FLT	'f'
#define COTS_LO_DBL	'd'

/**
 * User facing tick type.
 * This should be extended by the API user in accordance with the layout. */
struct cots_tick_s;

/**
 * User facing SoA ticks type. */
struct cots_tsoa_s;


/* public API */
/**
 * Create a time series object.
 * For LAYOUT parameter see description of LAYOUT slot for cots_ts_t.
 * For BLOCKZ parameter see description of BLOCKZ slot for cots_ts_t,
 * when BLOCKZ is 0, the default block size will be used. */
extern cots_ts_t make_cots_ts(const char *layout, size_t blockz);

/**
 * Free a time series object. */
extern void free_cots_ts(cots_ts_t);

/**
 * Attach backing FILE to series. */
extern int cots_attach(cots_ts_t, const char *file, int flags);

/**
 * Detach files from series, if any. */
extern int cots_detach(cots_ts_t);

/**
 * Open a cots-ts file. */
extern cots_ts_t cots_open_ts(const char *file, int flags);

/**
 * Close a cots-ts handle, the handle is unusable hereafter. */
extern int cots_close_ts(cots_ts_t);


/**
 * Return tag representation of STR (of length LEN). */
extern cots_tag_t cots_tag(cots_ts_t, const char *str, size_t len);


/**
 * Lodge field names (for documentation purposes) with TS.
 * An old array of fields in TS will be overwritten. */
extern int cots_put_fields(cots_ts_t, const char **fields);


/**
 * Write data tick to series.
 * Use TO parameter to record time offset.
 * Use TAG argument to tag this sample.
 * Optional arguments should coincide with the layout of the timeseries. */
extern int cots_write_va(cots_ts_t, ...);

/**
 * Write data tick to series.
 * The actual length of the tick is determined by the series' layout */
extern int cots_write_tick(cots_ts_t, const struct cots_tick_s*);

/**
 * Write N data ticks to series.
 * The actual length of the tick is determined by the series' layout */
extern int cots_write_ticks(cots_ts_t, const struct cots_tick_s*, size_t n);

/**
 * Read data tick from series, output to TGT. */
extern ssize_t cots_read_ticks(struct cots_tsoa_s *restrict, cots_ts_t);

#endif	/* INCLUDED_cotse_h_ */
