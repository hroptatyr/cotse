cotse
=====

cotse is a column-oriented temporal storage engine with a strong focus
on financial applications.


Features
--------

* [X] compressed
* [X] support for IEEE 754-2008 decimals


Red tape
--------

+ licence: [BSD3c][2]
+ github: <https://github.com/hroptatyr/cotse>
+ issues: <https://github.com/hroptatyr/cotse/issues>
+ releases: <https://github.com/hroptatyr/cotse/releases>
+ contributions: welcome

This is Pareto software, i.e. only the 80% case works.


Example
-------

To write some data to a file-backed tick store:

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
    cots_attach(db, "/tmp/tickdata", O_CREAT | O_TRUNC | O_RDWR);
    while (get_candle(&data)) {
    	cots_write_tick(db, &data.proto);
    }
    ...
    cots_detach(db);
    free_cots_ts(db);

And to read the contents:

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
