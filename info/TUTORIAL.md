cotse in 5 minutes
==================

This tutorial should get you a first impression on how to handle cots
files programmatically.


Writing
-------

For in-memory databases:

    #include <cotse.h>
    struct candle {
    	struct cots_tick_s proto;
    	cots_px_t o, h, l, c;
        cots_qx_t v;
    };
    ...
    struct candle data;
    cots_ts_t db = make_cots_ts("ppppq", 0U);
    cots_put_fields(db, (const char*[]){"open","high","low","close","volume"});
    while (get_candle(&data)) {
    	cots_write_tick(db, &data.proto);
    }
    ...
    free_cots_ts(db);


To make the dataset persistent attach a file at any time between
the calls of `make_cots_ts()` and `free_cots_ts()`.

    ...
    cots_attach(db, "/tmp/tickdata", O_CREAT | O_RDWR);
    ...

Data kept-in memory until this point will be flushed to the file
`/tmp/tickdata`.  Data written to the timeseries thereafter will
automatically go to that file too.


Reading
-------

For on-disk databases:

    #include <cotse.h>
    struct candle_soa {
    	struct cots_tsoa_s proto;
    	cots_px_t *o, *h, *l, *c;
    	cots_qx_t *v;
    };
    ...
    struct candle_soa dsoa;
    cots_ts_t db = cots_open_ts("/tmp/tickdata", O_RDONLY);
    ssize_t n;
    while ((n = cots_read_ticks(&dsoa.proto, db))) {
    	put_candle(&dsoa);
    };
    cots_close_ts(db);
