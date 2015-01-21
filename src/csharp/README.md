gRPC C#
=======

A C# implementation of gRPC, Google's RPC library.

EXPERIMENTAL ONLY
-----------------

**This gRPC C# implementation is work-in-progress and is not expected to work yet.**

- The implementation is a wrapper around gRPC C core library
- Code only runs under mono currently, because there have been issues building
  the gRPC C core library under Windows.
- It is very possible that some parts of the code will be heavily refactored or
  completely rewritten.

CONTENTS
--------

- ext:
  The extension library that wraps C API to be more digestible by C#.

