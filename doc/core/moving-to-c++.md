# Moving gRPC core to C++

October 2017

ctiller, markdroth, vjpai

## Background and Goal

gRPC core was originally written in C89 for several reasons
(possibility of kernel integration, ease of wrapping, compiler
support, etc). Over time, this was changed to C99 as all relevant
compilers in active use came to support C99 effectively.
[Now, gRPC core is C++](https://github.com/grpc/proposal/blob/master/L6-allow-c%2B%2B-in-grpc-core.md)
(although the code is still idiomatically C code) with C linkage for
public functions. Throughout all of these transitions, the public
header files are committed to remain in C89.

The goal now is to make the gRPC core implementation true idiomatic
C++ compatible with
[Google's C++ style guide](https://google.github.io/styleguide/cppguide.html).

## Constraints

- No use of standard library
  - Standard library makes wrapping difficult/impossible and also reduces platform portability
  - This takes precedence over using C++ style guide
- But lambdas are ok
- As are third-party libraries that meet our build requirements (such as many parts of abseil)
- There will be some C++ features that don't work
  - `new` and `delete`
  - pure virtual functions are not allowed because the message that prints out "Pure Virtual Function called" is part of the standard library
    - Make a `#define GRPC_ABSTRACT {GPR_ASSERT(false);}` instead of `= 0;`
- The sanity for making sure that we don't depend on libstdc++ is that at least some tests should explicitly not include it
  - Most tests can migrate to use gtest
    - There are tremendous # of code paths that can now be exposed to unit tests because of the use of gtest and C++
  - But at least some tests should not use gtest


## Roadmap

- What should be the phases of getting code converted to idiomatic C++
  - Opportunistically do leaf code that other parts don't depend on
  - Spend a little time deciding how to do non-leaf stuff that isn't central or polymorphic (e.g., timer, call combiner)
  - For big central or polymorphic interfaces, actually do an API review (for things like transport, filter API, endpoint, closure, exec_ctx, ...) .
    - Core internal changes don't need a gRFC, but core surface changes do
    - But an API review should include at least a PR with the header change and tests to use it before it gets used more broadly
  - iomgr polling for POSIX is a gray area whether it's a leaf or central
- What is the schedule?
  - In Q4 2017, if some stuff happens opportunistically, great; otherwise ¯\\\_(ツ)\_/¯
  - More updates as team time becomes available and committed to this project

## Implications for C++ API and wrapped languages

- For C++ structs, switch to `using` when possible (e.g., Slice,
ByteBuffer, ...)
- The C++ API implementation might directly start using
`grpc_transport_stream_op_batch` rather than the core surface `grpc_op`.
- Can we get wrapped languages to a point where we can statically link C++? This will take a year in probability but that would allow the use of `std::`
  - Are there other environments that don't support std library, like maybe Android NDK?
    - Probably, that might push things out to 18 months
