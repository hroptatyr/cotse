#include <fcntl.h>
#include <stdio.h>
#include <cotse.h>

#define countof(x)	(sizeof(x) / sizeof(*x))

struct candle {
    	struct cots_tsoa_s proto;
        cots_qx_t *q;
    	cots_px_t *p;
};

int main(int argc, char *argv[])
{
	struct candle c1, c2;
	size_t n1, n2;
	cots_ts_t db1, db2;

	if (argc <= 1 || argv[1U] == NULL) {
		return 2;
	}

	db1 = cots_open_ts(argv[1U], O_RDONLY);
	db2 = cots_open_ts(argv[1U], O_RDONLY);

	if (db1 == NULL) {
		fputs("cannot open db1\n", stderr);
		return 1;
	} else if (db2 == NULL) {
		fputs("cannot open db2\n", stderr);
		return 1;
	}

	/* read ticks */
	cots_init_tsoa(&c1.proto, db1);
	cots_init_tsoa(&c2.proto, db2);
	n1 = cots_read_ticks(&c1.proto, db1);
	n2 = cots_read_ticks(&c2.proto, db2);

	if (n1 != n2) {
		fprintf(stderr, "number of ticks differ, %zu v %zu\n", n1, n2);
		return 1;
	}

	for (size_t i = 0U; i < n1; i++) {
		printf("%lu\t%lu\n", c1.proto.toffs[i], c2.proto.toffs[i]);
	}

	cots_fini_tsoa(&c1.proto, db1);
	cots_fini_tsoa(&c2.proto, db2);

	cots_close_ts(db1);
	cots_close_ts(db2);
	return 0;
}
