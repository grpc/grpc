GRPC C STYLE GUIDE
=====================

Background
----------

Here we document style rules for C usage in the gRPC Core library.

General
-------

- Layout rules are defined by clang-format, and all code should be passed
  through clang-format. A (docker-based) script to do so is included in
  [tools/distrib/clang\_format\_code.sh](../tools/distrib/clang_format_code.sh).

Header Files
------------

- Public header files (those in the include/grpc tree) should compile as
  pedantic C89.
- Public header files should be includable from C++ programs. That is, they
  should include the following:
  ```c
  #ifdef __cplusplus
  extern "C" {
  # endif

  /* ... body of file ... */

  #ifdef __cplusplus
  }
  # endif
  ```
- Header files should be self-contained and end in .h.
- All header files should have a `#define` guard to prevent multiple inclusion.
  To guarantee uniqueness they should be based on the file's path.

  For public headers: `include/grpc/grpc.h` → `GRPC_GRPC_H`

  For private headers:
  `src/core/lib/channel/channel_stack.h` →
  `GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_H`

Variable Initialization
-----------------------

When declaring a (non-static) pointer variable, always initialize it to `NULL`.
Even in the case of static pointer variables, it's recommended to explicitly
initialize them to `NULL`.


C99 Features
------------

- Variable sized arrays are not allowed.
- Do not use the 'inline' keyword.
- Flexible array members are allowed
  (https://en.wikipedia.org/wiki/Flexible_array_member).

Comments
--------

Within public header files, only `/* */` comments are allowed.

Within implementation files and private headers, either single line `//`
or multi line `/* */` comments are allowed. Only one comment style per file is
allowed however (i.e. if single line comments are used anywhere within a file,
ALL comments within that file must be single line comments).

Symbol Names
------------

- Non-static functions must be prefixed by `grpc_`
- Static functions must *not* be prefixed by `grpc_`
- Typenames of `struct`s , `union`s, and `enum`s must be prefixed by `grpc_` if
  they are declared in a header file. They must not be prefixed by `grpc_` if
  they are declared in a source file.
- Enumeration values and `#define` names must be uppercase. All other values
  must be lowercase.
- Enumeration values or `#define` names defined in a header file must be
  prefixed with `GRPC_` (except for `#define` macros that are being used to
  substitute functions; those should follow the general rules for
  functions). Enumeration values or `#define`s defined in source files must not
  be prefixed with `GRPC_`.
- Multiple word identifiers use underscore as a delimiter, *never* camel
  case. E.g. `variable_name`.

Functions
----------

- The use of [`atexit()`](http://man7.org/linux/man-pages/man3/atexit.3.html) is
  in forbidden in libgrpc.
