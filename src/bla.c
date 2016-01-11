#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "dfp754_d32.h"
#include "dfp754_d64.h"
#include "cotse.h"
#include "nifty.h"

#define NSECS	(1000000000UL)
#define MSECS	(1000UL)
#define MSECQ	(1000000UL)

#define strtopx		strtod32
#define strtoqx		strtod64

struct samp_s {
	struct cots_tick_s proto;
	cots_px_t b;
	cots_qx_t q;
};

static struct samp_s
push(cots_ts_t ts, const char *line, size_t UNUSED(llen))
{
	char *on;
	long unsigned int s, x;
	cots_px_t b;
	cots_qx_t q;
	cots_tag_t m;

	if (line[20U] != '\t') {
		return (struct samp_s){{0U, 0U}};
	} else if (!(s = strtoul(line, &on, 10))) {
		return (struct samp_s){{0U, 0U}};
	} else if (*on++ != '.') {
		return (struct samp_s){{0U, 0U}};
	} else if ((x = strtoul(on, &on, 10), *on != '\t')) {
		return (struct samp_s){{0U, 0U}};
	}
	s = (s * NSECS + x);
	//s /= MSECQ;
	//s *= MSECQ;

	with (const char *ecn = ++on) {
		if (UNLIKELY((on = strchr(ecn, '\t')) == NULL)) {
			return (struct samp_s){{0U, 0U}};
		} else if (UNLIKELY(!(m = cots_tag(ts, ecn, on - ecn)))) {
			/* fuck */
			return (struct samp_s){{0U, 0U}};
		}
	}

	if (*++on == '\t' || isnand32(b = strtopx(on, &on))) {
		return (struct samp_s){{0U, 0U}};
	}

	if (*++on == '\t' || isnand64(q = strtoqx(on, &on))) {
		return (struct samp_s){{0U, 0U}};
	}
	return (struct samp_s){{s, m}, b, q};
}

int
main(int argc, char *argv[])
{
	static size_t i;
	char *line = NULL;
	size_t llen = 0U;
	cots_ts_t db;

	db = make_cots_ts("pq");
	for (ssize_t nrd; (nrd = getline(&line, &llen, stdin)) > 0;) {
		struct samp_s x = push(db, line, nrd);

		if (x.proto.tag) {
			//cots_write_va(db, x.proto.toff, x.proto.tag, x.b, x.q);
			cots_write_tick(db, &x.proto);
		}
		if (++i >= 1000000) {
			break;
		}
	}
	close(STDOUT_FILENO);
	free(line);
	free_cots_ts(db);
	return 0;
}
