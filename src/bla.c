#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "dfp754_d32.h"
#include "comp-to.h"
#include "comp-px.h"
#include "nifty.h"

#define NSECS	(1000000000UL)
#define MSECS	(1000UL)
#define MSECQ	(1000000UL)

typedef _Decimal32 px_t;
#define strtopx		strtod32

static cots_to_t them[16384U];
static px_t bids[16384U];
static size_t nthem;
static unsigned char data[sizeof(them)];

static void
push(const char *line, size_t UNUSED(llen))
{
	char *on;
	long unsigned int s, x;
	px_t b;

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
	them[nthem] = s;


	with (const char *ecn = ++on) {
		if (UNLIKELY((on = strchr(ecn, '\t')) == NULL)) {
			return;
		}
	}

	if (*++on != '\t' && (b = strtopx(on, &on))) {
		bids[nthem] = b;
	}
	nthem++;
	return;
}

static void
pr(void)
{
#if 0
	cots_to_t last = 0U;

	for (size_t i = 0U; i < 4U; i++) {
		fprintf(stderr, "<- %+ld  <- %lu\n", them[i], them[i] - last);
		last = them[i];
	}
	for (size_t i = 124U; i < 132U; i++) {
		fprintf(stderr, "<- %+ld  <- %lu\n", them[i], them[i] - last);
		last = them[i];
	}
	for (size_t i = 8188U; i < 8192U; i++) {
		fprintf(stderr, "<- %+ld  <- %lu\n", them[i], them[i] - last);
		last = them[i];
	}
#endif
	return;
}

static void
prpx(void)
{
#if 1
	char sp[32U];
	for (size_t i = 0U; i < 4U; i++) {
		d32tostr(sp, sizeof(sp), bids[i]);
		fputs(sp, stderr);
		fputc('\n', stderr);
	}
	for (size_t i = 124U; i < 132U; i++) {
		d32tostr(sp, sizeof(sp), bids[i]);
		fputs(sp, stderr);
		fputc('\n', stderr);
	}
	for (size_t i = 8188U; i < 8192U; i++) {
		d32tostr(sp, sizeof(sp), bids[i]);
		fputs(sp, stderr);
		fputc('\n', stderr);
	}
#endif
}

int
main(int argc, char *argv[])
{
	char *line = NULL;
	size_t llen = 0U;

	memset(data, 0xef, sizeof(data));
	for (ssize_t nrd; (nrd = getline(&line, &llen, stdin)) > 0;) {
		push(line, nrd);

		if (nthem >= countof(them)) {
#if 0
			size_t z = comp_to(data, them, countof(them));
			size_t n;

			fprintf(stderr, "%zu -> %zu\n", sizeof(them), z);
			pr();

			memset(them, 0, sizeof(them));

			/* decode */
			n = dcmp_to(them, data, z);
			fprintf(stderr, "%zu -> %zu\n", z, n * sizeof(*them));

			pr();

			write(STDOUT_FILENO, data, z);

#else
			size_t z = comp_px(data, bids, countof(bids));
			size_t n;

			fprintf(stderr, "%zu -> %zu\n", sizeof(bids), z);
			prpx();

			memset(bids, 0, sizeof(bids));

			/* decode */
			n = dcmp_px(bids, data, z);
			fprintf(stderr, "%zu -> %zu\n", z, n * sizeof(*bids));

			prpx();

			write(STDOUT_FILENO, data, z);
#endif
			nthem = 0U;
		}
	}
	close(STDOUT_FILENO);
	free(line);
	return 0;
}
