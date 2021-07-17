# Moving gRPC core to C++

Originally written by ctiller, markdroth, and vjpai in October 2017

Revised by veblush in October 2019

## Background and Goal

gRPC core was originally written in C89 for several reasons
(possibility of kernel integration, ease of wrapping, compiler
support, etc). Over time, this was changed to C99 as all relevant
compilers in active use came to support C99 effectively.

gRPC started allowing to use C++ with a couple of exceptions not to
have C++ library linked such as `libstdc++.so`.
(For more detail, see the [proposal](https://github.com/grpc/proposal/blob/master/L6-core-allow-cpp.md))

Finally gRPC became ready to use full C++11 with the standard library by the [proposal](https://github.com[/grpc/proposal/blob/master/L59-core-allow-cppstdlib.md).

Throughout all of these transitions, the public header files are committed to remain in C89.

The goal now is to make the gRPC core implementation true idiomatic
C++ compatible with
[Google's C++ style guide](https://google.github.io/styleguide/cppguide.html).

## Constraints

- Most of features available in C++11 are allowed to use but there are some exceptions
  because gRPC should support old systems.
  - Should be built with gcc 4.8, clang 3.3, and Visual C++ 2015.
  - Should be run on Linux system with libstdc++ 6.0.9 to support
    [manylinux1](https://www.python.org/dev/peps/pep-0513).
- This would limit us not to use modern C++11 standard library such as `filesystem`.
  You can easily see whether PR is free from this issue by checking the result of
  `Artifact Build Linux` test.
- `thread_local` is not allowed to use on Apple's products because their old OSes
  (e.g. ios < 9.0) don't support `thread_local`. Please use `GPR_TLS_DECL` instead.
- gRPC main libraries (grpc, grpc+++, and plugins) cannot use following C++ libraries:
  (Test and example codes are relatively free from this constraints)
  - `<thread>`. Use `grpc_core::Thread`.
  - `<condition_variable>`. Use `grpc_core::CondVar`.
  - `<mutex>`. Use `grpc_core::Mutex`, `grpc_core::MutexLock`, and `grpc_core::ReleasableMutexLock`.
  - `<future>`
  - `<ratio>`
  - `<system_error>`
  - `<filesystem>`
- `grpc_core::Atomic` is prefered over `std::atomic` in gRPC library because it provides
  additional debugging information.

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
