#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "comp-to.h"
#include "nifty.h"

#define NSECS	(1000000000UL)
#define MSECS	(1000UL)
#define MSECQ	(1000000UL)

static long unsigned int them[16384U];
static size_t nthem;
static unsigned char data[sizeof(them)];

static void
push(const char *line, size_t UNUSED(llen))
{
	char *on;
	long unsigned int s, x;

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
	them[nthem++] = s;
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
	for (size_t i = 252U; i < 256U; i++) {
		fprintf(stderr, "<- %+ld  <- %lu\n", them[i], them[i] - last);
		last = them[i];
	}
#endif
	return;
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

			nthem = 0U;
		}
	}
	close(STDOUT_FILENO);
	free(line);
	return 0;
}
