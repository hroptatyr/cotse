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

static cots_to_t them[16384U];
static cots_mtrc_t mtrs[16384U];
static cots_px_t bids[16384U];
static cots_qx_t qtys[16384U];
static size_t nthem;
static unsigned char data[sizeof(them)];
static cots_ob_t obar;

static void
push(const char *line, size_t UNUSED(llen))
{
	char *on;
	long unsigned int s, x;
	cots_px_t b;
	cots_qx_t q;
	cots_mtrc_t m;

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
		} else if (UNLIKELY(!(m = cots_intern(obar, ecn, on - ecn)))) {
			/* fuck */
			return;
		}
		mtrs[nthem] = m;
	}

	if (*++on != '\t' && (b = strtopx(on, &on))) {
		bids[nthem] = b;
	}

	if (*++on != '\t' && (q = strtoqx(on, &on))) {
		qtys[nthem] = q;
	}
	nthem++;
	return;
}

static void
pr(void)
{
#if 1
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

static void
prqx(void)
{
#if 1
	char sp[64U];
	for (size_t i = 0U; i < 4U; i++) {
		d64tostr(sp, sizeof(sp), qtys[i]);
		fputs(sp, stderr);
		fputc('\n', stderr);
	}
	for (size_t i = 124U; i < 164U; i++) {
		d64tostr(sp, sizeof(sp), qtys[i]);
		fputs(sp, stderr);
		fputc('\n', stderr);
	}
	for (size_t i = 8188U; i < 8192U; i++) {
		d64tostr(sp, sizeof(sp), qtys[i]);
		fputs(sp, stderr);
		fputc('\n', stderr);
	}
#endif
}

static void
prhx(cots_ob_t ob)
{
#if 1
	for (size_t i = 0U; i < 4U; i++) {
		fprintf(stderr, "%lx\t%s\n", mtrs[i], cots_mtrc_name(ob, mtrs[i]));
	}
	for (size_t i = 124U; i < 164U; i++) {
		fprintf(stderr, "%lx\t%s\n", mtrs[i], cots_mtrc_name(ob, mtrs[i]));
	}
	for (size_t i = 8188U; i < 8192U; i++) {
		fprintf(stderr, "%lx\t%s\n", mtrs[i], cots_mtrc_name(ob, mtrs[i]));
	}
#endif
}

int
main(int argc, char *argv[])
{
	char *line = NULL;
	size_t llen = 0U;

	obar = make_cots_ob();
	memset(data, 0xef, sizeof(data));
	for (ssize_t nrd; (nrd = getline(&line, &llen, stdin)) > 0;) {
		push(line, nrd);

		if (nthem >= countof(them)) {
#if 1
			size_t z = comp_to(data, them, countof(them));
			size_t n;

			fprintf(stderr, "%zu -> %zu\n", sizeof(them), z);
			pr();

			memset(them, 0, sizeof(them));

			/* decode */
			n = dcmp_to(them, data, z);
			fprintf(stderr, "%zu -> %zu\n", z, n * sizeof(*them));

			pr();

#elif 0
			size_t z = comp_px(data, bids, countof(bids));
			size_t n;

			fprintf(stderr, "%zu -> %zu\n", sizeof(bids), z);
			prpx();

			memset(bids, 0, sizeof(bids));

			/* decode */
			n = dcmp_px(bids, data, z);
			fprintf(stderr, "%zu -> %zu\n", z, n * sizeof(*bids));

			prpx();

#elif 0
			size_t z = comp_qx(data, qtys, countof(qtys));
			size_t n;

			fprintf(stderr, "%zu -> %zu\n", sizeof(qtys), z);
			prqx();

			memset(qtys, 0, sizeof(qtys));

			/* decode */
			n = dcmp_qx(qtys, data, z);
			fprintf(stderr, "%zu -> %zu\n", z, n * sizeof(*qtys));

			prqx();

#elif 1
			size_t z, oz;
			size_t n;

			oz = z = comp_ob(data, obar);
			z += comp_mtrc(data + oz, mtrs, countof(mtrs));

			fprintf(stderr, "%zu -> %zu\n", sizeof(mtrs), z);
			prhx(obar);

			memset(mtrs, 0, sizeof(mtrs));

			/* decode */
			cots_ob_t yay = dcmp_ob(data, oz);
			n = dcmp_mtrc(mtrs, data + oz, z - oz);
			fprintf(stderr, "%zu -> %zu\n", z, n * sizeof(*mtrs));

			prhx(yay);

#endif
			write(STDOUT_FILENO, data, z);
			nthem = 0U;
			break;
		}
	}
	close(STDOUT_FILENO);
	free(line);
	free_cots_ob(obar);
	return 0;
}
