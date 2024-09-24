# Utility Code

The files in this directory contain various utility libraries and platform
abstractions for C++ code.  None of this code is gRPC-specific; anything
here may also be useful for other open source projects written in C++.
In principle, any library here could be replaced with an external
dependency that provides the same functionality if such an external
library should become available.

Note that this is one of the few places in src/core where we allow
the use of portability macros.
