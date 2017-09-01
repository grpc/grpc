Nanopb example "using_double_on_avr"
====================================

Some processors/compilers, such as AVR-GCC, do not support the double
datatype. Instead, they have sizeof(double) == 4. Because protocol
binary format uses the double encoding directly, this causes trouble
if the protocol in .proto requires double fields.

This directory contains a solution to this problem. It uses uint64_t
to store the raw wire values, because its size is correct on all
platforms. The file double_conversion.c provides functions that
convert these values to/from floats, without relying on compiler
support.

To use this method, you need to make some modifications to your code:

1) Change all 'double' fields into 'fixed64' in the .proto.

2) Whenever writing to a 'double' field, use float_to_double().

3) Whenever reading a 'double' field, use double_to_float().

The conversion routines are as accurate as the float datatype can
be. Furthermore, they should handle all special values (NaN, inf, denormalized
numbers) correctly. There are testcases in test_conversions.c.
