# Slice

This directory contains the implementation of the gRPC slice library, which
provides a set of data structures for representing and manipulating blocks of
memory.

## Overarching Purpose

The slice library is a core component of gRPC's C-core. It is used extensively
throughout the gRPC codebase to represent message payloads, metadata, and other
data. Its primary purpose is to provide a safe, efficient, and flexible way to
manage memory.

## Core Concepts

The slice library is built around a few key abstractions:

*   **`Slice`**: A `Slice` is a reference-counted, immutable string-like object. It is the most common type of slice, and it is used to represent data that is shared between multiple parts of the codebase.
*   **`MutableSlice`**: A `MutableSlice` is a slice that is guaranteed to have a unique owner. This means that the underlying data can be mutated safely.
*   **`StaticSlice`**: A `StaticSlice` is a slice that points to a
    statically-allocated block of memory. It is not reference-counted, and it is
    very fast to copy.
*   **`SliceBuffer`**: A `SliceBuffer` is a container for a collection of `Slice`
    objects. It is used to represent a sequence of slices, such as a message.

The key to understanding the slice library is to understand the ownership model.
A `Slice` can be either "owned" or "unowned". An owned slice has a reference
count, and the underlying memory is freed when the reference count drops to zero.
An unowned slice does not have a reference count, and the underlying memory is
not freed when the slice is destroyed. The type of a slice determines its
ownership model.

## Performance

The slice library is a key component of gRPC's performance and memory
efficiency. It allows gRPC to avoid unnecessary data copies and to manage memory
in a more efficient way. For example, when a message is received, it can be
represented as a `SliceBuffer` of `Slice`s. Each `Slice` can point to a different
part of the underlying network buffer. This allows the message to be processed
without having to copy the data into a contiguous block of memory.

## Files

*   **`slice.h`, `slice.cc`**: These files define the `Slice`, `MutableSlice`, and
    `StaticSlice` classes.
*   **`slice_buffer.h`, `slice_buffer.cc`**: These files define the `SliceBuffer` class.
*   **`slice_internal.h`**: This file contains the internal implementation details
    of the slice library.
*   **`slice_refcount.h`**: This file defines the `grpc_slice_refcount` class,
    which is used to manage the reference count of a slice.
*   **`percent_encoding.h`, `percent_encoding.cc`**: These files provide
    functions for percent-encoding and decoding slices. This is used to encode
    binary data in a way that is safe for transmission over HTTP/2.

## Major Classes

*   `grpc_core::Slice`: A reference-counted, immutable string-like object.
*   `grpc_core::MutableSlice`: A slice with a unique owner, allowing for safe mutation.
*   `grpc_core::StaticSlice`: A slice that points to statically-allocated memory.
*   `grpc_core::SliceBuffer`: A container for a collection of `Slice` objects.

## Notes

*   The slice library is a low-level component of the gRPC stack. Most
    application developers will not need to interact with it directly.
*   The `Slice` and `SliceBuffer` classes are designed to be thread-safe.
*   The slice library is a good example of how gRPC uses a combination of C and
    C++ to achieve high performance and memory efficiency. The core data
    structures are implemented in C, and then wrapped in C++ classes to provide a
    more convenient and safe API.
