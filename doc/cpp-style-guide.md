GRPC C++ STYLE GUIDE
=====================

Background
----------

Here we document style rules for C++ usage in the gRPC C++ bindings
and tests.

General
-------

- The majority of gRPC's C++ requirements are drawn from the [Google C++ style
guide] (https://google.github.io/styleguide/cppguide.html)
   - However, gRPC has some additional requirements to maintain
     [portability] (#portability)
- As in C, layout rules are defined by clang-format, and all code
should be passed through clang-format. A (docker-based) script to do
so is included in [tools/distrib/clang\_format\_code.sh]
(../tools/distrib/clang_format_code.sh).

<a name="portability"></a>
Portability Restrictions
-------------------

gRPC supports a large number of compilers, ranging from those that are
missing many key C++11 features to those that have quite detailed
analysis. As a result, gRPC compiles with a high level of warnings and
treat all warnings as errors. gRPC also forbids the use of some common
C++11 constructs. Here are some guidelines, to be extended as needed:
- Do not use range-based for. Expressions of the form
  ```c
  for (auto& i: vec) {
    // code
  }
  ```
  
  are not allowed and should be replaced with code such as
  ```c
  for (auto it = vec.begin; it != vec.end(); it++) {
    auto& i = *it;
    // code
  }
  ```
  
- Do not use lambda of any kind (no capture, explicit capture, or
default capture). Other C++ functional features such as
`std::function` or `std::bind` are allowed
- Do not use brace-list initializers.
- Do not compare a pointer to `nullptr` . This is because gcc 4.4
  does not support `nullptr` directly and gRPC implements a subset of
  its features in [include/grpc++/impl/codegen/config.h]
  (../include/grpc++/impl/codegen/config.h). Instead, pointers should
  be checked for validity using their implicit conversion to `bool`.
  In other words, use `if (p)` rather than `if (p != nullptr)`
- Do not use `final` or `override` as these are not supported by some
  compilers. Instead use `GRPC_FINAL` and `GRPC_OVERRIDE` . These
  compile down to the traditional C++ forms for compilers that support
  them but are just elided if the compiler does not support those features.
- In the [include] (../../../tree/master/include/grpc++) and [src]
  (../../../tree/master/src/cpp) directory trees, you should also not
  use certain STL objects like `std::mutex`, `std::lock_guard`,
  `std::unique_lock`, `std::nullptr`, `std::thread` . Instead, use
  `grpc::mutex`, `grpc::lock_guard`, etc., which are gRPC
  implementations of the prominent features of these objects that are
  not always available. You can use the `std` versions of those in  [test]
  (../../../tree/master/test/cpp)
- Similarly, in the same directories, do not use `std::chrono` unless
  it is guarded by `#ifndef GRPC_CXX0X_NO_CHRONO` . For platforms that
  lack`std::chrono,` there is a C-language timer called gpr_timespec that can
  be used instead.
- `std::unique_ptr` must be used with extreme care in any kind of
  collection. For example `vector<std::unique_ptr>` does not work in
  gcc 4.4 if the vector is constructed to its full size at
  initialization but does work if elements are added to the vector
  using functions like `push_back`. `map` and other pair-based
  collections do not work with `unique_ptr` under gcc 4.4. The issue
  is that many of these collection implementations assume a copy
  constructor
  to be available.
- Don't use `std::this_thread` . Use `gpr_sleep_until` for sleeping a thread.
- [Some adjacent character combinations cause problems]
  (https://en.wikipedia.org/wiki/Digraphs_and_trigraphs#C). If declaring a
  template against some class relative to the global namespace,
  `<::name` will be non-portable. Separate the `<` from the `:` and use `< ::name`.
