#include <fcntl.h>
#include <cotse.h>

#define countof(x)	(sizeof(x) / sizeof(*x))

/* must coincide with creat_01 */
struct candle {
    	struct cots_tick_s proto;
        cots_qx_t q;
    	cots_px_t p;
};

static const struct candle data[] = {
	{{1073938234U}, 10.dd, 405.770000df},
	{{1073938256U}, 1.dd, 405.580100df},
	{{1073938271U}, 56.dd, 405.570100df},
	{{1073938282U}, 1.dd, 405.500000df},
	{{1073938292U}, 2.dd, 405.480000df},
	{{1073938301U}, 11.dd, 405.120100df},
	{{1073938324U}, 1.dd, 405.080100df},
	{{1073938332U}, 1874.dd, 405.000000df},
	{{1073938340U}, 1873.dd, 405.000000df},
	{{1073938352U}, 1623.dd, 405.000000df},
	{{1073938367U}, 1622.dd, 405.000000df},
	{{1073938380U}, 1602.dd, 405.000000df},
	{{1073938392U}, 582.dd, 405.000000df},
	{{1073938406U}, 581.dd, 405.000000df},
	{{1073938457U}, 571.dd, 405.000000df},
	{{1073938457U}, 566.dd, 405.000000df},
	{{1073938457U}, 466.dd, 405.000000df},
	{{1073938466U}, 434.dd, 405.000000df},
	{{1073938505U}, 2.dd, 408.010000df},
	{{1073938652U}, 416.dd, 408.280000df},
	{{1073938681U}, 62.dd, 410.040000df},
	{{1073938857U}, 472.dd, 410.040000df},
	{{1073939009U}, 107.dd, 410.000000df},
	{{1073939009U}, 48.dd, 410.000000df},
	{{1073939239U}, 2.dd, 408.010000df},
	{{1073939490U}, 39.dd, 407.640100df},
	{{1073939851U}, 398.dd, 405.010000df},
	{{1073939950U}, 455.dd, 405.010000df},
	{{1073940265U}, 57.dd, 405.010000df},
	{{1073940293U}, 265.dd, 405.010000df},
	{{1073940371U}, 272.dd, 405.020000df},
	{{1073940503U}, 335.dd, 405.020000df},
	{{1073940738U}, 63.dd, 405.020000df},
	{{1073940760U}, 219.dd, 405.020000df},
	{{1073940811U}, 221.dd, 405.030000df},
	{{1073941498U}, 283.dd, 405.030000df},
	{{1073941514U}, 62.dd, 405.030000df},
	{{1073941526U}, 156.dd, 405.020000df},
	{{1073941534U}, 165.dd, 405.010000df},
	{{1073941554U}, 103.dd, 405.010000df},
	{{1073941585U}, 434.dd, 405.000000df},
	{{1073941587U}, 91.dd, 405.000000df},
	{{1073941587U}, 41.dd, 405.000000df},
	{{1073941587U}, 1.dd, 405.000000df},
	{{1073941599U}, 1.dd, 404.929900df},
	{{1073941651U}, 60.dd, 404.919900df},
	{{1073941651U}, 102.dd, 404.790000df},
	{{1073941651U}, 1.dd, 404.730000df},
	{{1073941660U}, 62.dd, 404.659900df},
	{{1073941679U}, 6.dd, 404.649900df},
	{{1073941686U}, 57.dd, 404.600100df},
	{{1073941697U}, 106.dd, 404.590100df},
	/* going backwards now */
	{{1073931699U}, 1.dd, 404.580100df},
	{{1073931728U}, 260.dd, 404.540000df},
	{{1073931775U}, 2101.dd, 404.500000df},
	{{1073931775U}, 2100.dd, 404.500000df},
};

int main(void)
{
	cots_ts_t db = cots_open_ts("appnd_01.cots", O_RDWR);

	for (size_t i = 0U; i < countof(data); i++) {
		cots_write_tick(db, &data[i].proto);
	}
	cots_detach(db);
	free_cots_ts(db);
	return 0;
}
