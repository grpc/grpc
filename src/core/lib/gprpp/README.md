# GPR++ - Google Portable Runtime for C++

The files in this directory contain various utility code for C++ code.
None of this code is gRPC-specific; anything here may also be useful
for other open source projects written in C++.

Note that this is one of the few places in src/core where we allow
the use of portability macros.

Note that this is the only place in src/core where we allow
use of the C++ standard library (i.e., anything in the `std::`
namespace).  And for now, we require that any use of the
standard library is build-time-only -- i.e., we do not allow
run-time dependencies on libstdc++.  For more details, see
[gRFC L6](/grpc/proposal/blob/master/L6-allow-c%2B%2B-in-grpc-core.md) and
[Moving gRPC core to C++](/grpc/grpc/blob/master/doc/core/moving-to-c%2B%2B.md).
