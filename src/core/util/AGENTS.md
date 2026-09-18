# gRPC Utilities

This directory contains a collection of utility classes and functions that are used throughout the gRPC core.

## Overarching Purpose

This directory provides a set of common utilities that are not specific to any particular part of gRPC. This includes things like string manipulation, data structures, and platform-specific code.

## Files and Subdirectories

- **`alloc.h` / `alloc.cc`**: Memory allocation utilities.
- **`atomic_utils.h`**: Utilities for working with atomic operations.
- **`backoff.h` / `backoff.cc`**: Implements exponential backoff.
- **`crash.h` / `crash.cc`**: Utilities for crashing the process.
- **`env.h`**: Functions for getting and setting environment variables.
- **`fork.h` / `fork.cc`**: Functions for handling `fork()`.
- **`host_port.h` / `host_port.cc`**: Utilities for joining and splitting host and port strings.
- **`json/`**: A JSON parser and writer.
- **`log.cc`**: The implementation of gpr_log.
- **`orphanable.h`**: The `Orphanable` class, which is a base class for objects that can be orphaned.
- **`ref_counted.h`**: The `RefCounted` class, which is a base class for reference-counted objects.
- **`ref_counted_ptr.h`**: A smart pointer for `RefCounted` objects.
- **`status_helper.h` / `status_helper.cc`**: Utilities for creating `grpc_status_code` and `grpc_slice` instances.
- **`string.h` / `string.cc`**: String manipulation functions.
- **`sync.h` / `sync.cc`**: Synchronization primitives.
- **`time.h` / `time.cc`**: Time-related functions.
- **`uri.h` / `uri.cc`**: A URI parser.
- **`work_serializer.h` / `work_serializer.cc`**: A class that serializes callbacks so that only one runs at a time, in order.

## Notes

- This directory contains a lot of code, much of which is highly specialized.
- When looking for a particular utility, it is often helpful to search for the relevant keywords in this directory.
- The code in this directory is generally well-documented, so it is often possible to understand what a particular utility does by reading the comments in the header file.
- [README.md](README.md) in this directory also has useful information.