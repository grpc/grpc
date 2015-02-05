gRPC C#
=======

A C# implementation of gRPC, Google's RPC library.

EXPERIMENTAL ONLY
-----------------

**This gRPC C# implementation is work-in-progress and is not expected to work yet.**

- The implementation is a wrapper around gRPC C core library
- Code only runs under mono currently, building gGRPC C core library under Windows
  is in progress.
- It is very possible that some parts of the code will be heavily refactored or
  completely rewritten.

CONTENTS
--------

- ext:
  The extension library that wraps C API to be more digestible by C#.
- GrpcCore:
  The main gRPC C# library.
- GrpcApi:
  API examples for math.proto.

