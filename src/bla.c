#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "dfp754_d32.h"
#include "dfp754_d64.h"
#include "comp-to.h"
#include "comp-px.h"
#include "comp-qx.h"
#include "comp-ob.h"
#include "intern.h"
#include "nifty.h"

#define NSECS	(1000000000UL)
#define MSECS	(1000UL)
#define MSECQ	(1000000UL)

#define strtopx		strtod32
#define strtoqx		strtod64

static cots_ob_t obar;

static void
push(const char *line, size_t UNUSED(llen))
{
	char *on;
	long unsigned int s, x;
	cots_px_t b;
	cots_qx_t q;
	cots_tag_t m;

	if (line[20U] != '\t') {
		return;
	} else if (!(s = strtoul(line, &on, 10))) {
		return;
	} else if (*on++ != '.') {
		return;
	} else if ((x = strtoul(on, &on, 10), *on != '\t')) {
		return;
	}
	s = (s * NSECS + x);
	//s /= MSECQ;
	//s *= MSECQ;

	with (const char *ecn = ++on) {
		if (UNLIKELY((on = strchr(ecn, '\t')) == NULL)) {
			return;
		} else if (UNLIKELY(!(m = cots_intern(obar, ecn, on - ecn)))) {
			/* fuck */
			return;
		}
	}

	if (*++on == '\t' || isnand32(b = strtopx(on, &on))) {
		return;
	}

	if (*++on == '\t' || isnand64(q = strtoqx(on, &on))) {
		return;
	}

	cots_push(NULL, m, s, b, q);
	return;
}

int
main(int argc, char *argv[])
{
	char *line = NULL;
	size_t llen = 0U;

	obar = make_cots_ob();
	for (ssize_t nrd; (nrd = getline(&line, &llen, stdin)) > 0;) {
		push(line, nrd);
	}
	close(STDOUT_FILENO);
	free(line);
	free_cots_ob(obar);
	return 0;
}
