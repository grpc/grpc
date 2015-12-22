GRPC C STYLE GUIDE
=====================

Background
----------

Here we document style rules for C usage in the gRPC Core library.

General
-------

- Layout rules are defined by clang-format, and all code should be passed through
  clang-format. A (docker-based) script to do so is included in 
  tools/distrib/clang_format_code.sh.

Header Files
------------

- Public header files (those in the include/grpc tree) should compile as pedantic C89
- Header files should be self-contained and end in .h.
- All header files should have a #define guard to prevent multiple inclusion.
  To guarantee uniqueness they should be based on the file's path.

  For public headers: include/grpc/grpc.h --> GRPC_GRPC_H

  For private headers: 
  src/core/channel/channel_stack.h --> GRPC_INTERNAL_CORE_CHANNEL_CHANNEL_STACK_H

C99 Features
------------

- Variable sized arrays are not allowed
- Do not use the 'inline' keyword

Symbol Names
------------

- Non-static functions must be prefixed by grpc_
- static functions must not be prefixed by grpc_
- enumeration values and #define names are uppercased, all others are lowercased
- Multiple word identifiers use underscore as a delimiter (NEVER camel casing)
