cotse file format
=================

While it would have been nice to reuse existing technology, such as
influxdata's TSM files or simply being built upon berkdb, our way of
thinking about time series data isn't quite compatible.  This is not to
say we rule out this option for good but at the moment, and with regard
to time to market, it seems more feasible to quickly draft up a file
format of our own.

From a distance cotse files look like

    +--------+--------+------------+------++-------++
    | header | layout |series data | meta || index ||
    +--------+--------+------------+------++-------++

where HEADER is exactly 32 bytes long, LAYOUT, SERIES DATA and META are
of variable length and INDEX is implemented as another cots time series
with its own header, series data and meta part, cf. Index section.

The order and width of these components has been chosen carefully with
regard to mutability.  The header is mutable (it contains offsets) but
is fixed-width and, thusly, to guarantee modifying it is easy it got the
spot at the beginning of the file, well, as if calling it header wasn't
reason enough.

Layout, while of variable width, is immutable once it's manifested, and
being necessary to interpret series data explains the choice for its
location.  Series data is of variable width but append-only so it can go
right behind the fixed-width and immutable bits and bobs.

The meta section and the index are both mutable and variable in width,
so they got the spot behind the series data.  For associativity reasons
the meta section comes first: Index as mentioned above is just another
cots time series with its own header, data and meta part.  Having meta
come behind the index bit means we *include* another series rather than
*concatenate* two series.  Concatenation allows for a dense view:

    AB = A(C(D))  for B = CD

vs. inclusion:

    ABa = A(C(Dd)c)a  for B = CD

with lower case letters being the meta parts of their respective upper
case letter.  In order to decide which is better we examined the two
options exhaustively using a stochastic technique known as best-of-3
coin flipping.  After about 3 flips it became clear the former variant
is superior to the second even though there's no intricate technical
reason to choose one or the other.

Let's look at the components in greater detail.


Header
------

As you can see we love those diagrams, so let's explain the header
bits with those:

    +-------+---------+--------+-------+-------------+-------------+
    | magic | version | endian | flags | meta offset | next offset |
    +-------+---------+--------+-------+-------------+-------------+

MAGIC is the 4-byte string literal `cots` (in hex `0x636f7473`).

VERSION is a 2-byte string literal, at the moment `v0` (hex `0x7530`)
but any 2-byte literals are reserved.

ENDIAN is the uint16_t integer literal `0x3c3e` written in the
producer's native byte order, so it will come out as `<>` on big endian
machines and `><` on little endian machines.  This theoretically limits
cotse to those two architectures, leaving middle-endian systems behind,
but in practice at least the series data block should be invariant to
endianness (as it is a bit packing procedure) and, with the exception
of the ENDIAN field, all integer literals in the file shall be written
in big endian order.  This indicator however might help with user data
in the meta section or future versions of the file format.

FLAGS is a bit field of size 64:

    +------------------------------------------------------------------+
    | @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ILLLL |
    +------------------------------------------------------------------+

with bit marked `@` being reserved and mandated to be 0 at the moment.
The `I` bit, if set, indicates an index.  The `L` bits hold the log2 of
the block size (minus 9), so `0000` stands for a block size of 512, the
minimum, and `1111` stands for the maximum block size, 16777216.

META OFFSET is the offset (uint64_t in big endian order) into the meta
section, i.e. immediately behind series data, measured from the
beginning of the file in bytes. 

NEXT OFFSET is the uint64_t (big endian) offset in bytes from the
beginning of the file pointing to the next series, most of the time this
would be an index series.


Layout
------

In the layout section we manifest the column data types.  They
correspond one to one to the order used in the series data and they're
one byte a type.  The primary time stamp column is implicit.

    t  time stamp values, uint64_t
    s  interned string, uint64_t
    p  price values, _Decimal32
    f  generic floats, _Float32_t
    q  quantity values, _Decimal64
    d  generic doubles, _Float64_t
    c  counts, monotonical, uint64_t
    z  sizes, uint64_t

The layout is stored as \nul-terminated string in the layout section.


Series data
-----------


Meta
----


Index
-----

The index section is a cots time series with layout `(t)cz` and block
size a sixteenth of the block size of the series to be indexed.
