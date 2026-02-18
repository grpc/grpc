# In-Process Transport

This directory contains an in-process transport implementation for gRPC.

See also: [gRPC Transports overview](../../transport/GEMINI.md)

## Overarching Purpose

The in-process transport provides a mechanism for a client and server to communicate within the same process. This is primarily used for testing purposes, as it allows for end-to-end testing of gRPC applications without the need for a network connection. It can also be useful for building tightly-coupled services that need to communicate with each other in a low-latency, high-performance way.

## Files

*   `inproc_transport.h`, `inproc_transport.cc`: These files contain the modern, `Transport`-based implementation of the in-process transport. This is the recommended implementation for new code.
*   `legacy_inproc_transport.h`, `legacy_inproc_transport.cc`: These files contain the legacy implementation of the in-process transport. This implementation is preserved for backwards compatibility.

## Major Functions

*   `grpc_core::MakeInProcessTransportPair`: Creates a pair of connected in-process transports. This is the main entry point for the modern implementation.
*   `grpc_inproc_channel_create`: Creates an in-process channel. This is a convenience function that wraps `MakeInProcessTransportPair`.
*   `grpc_legacy_inproc_channel_create`: Creates a legacy in-process channel. This is the main entry point for the legacy implementation.

## Notes

*   The in-process transport is a powerful tool for testing gRPC applications. It allows for fast and reliable end-to-end testing without the overhead of a network connection.
*   The modern implementation of the in-process transport is a good example of how to implement a custom transport for gRPC. It is a relatively simple transport, and it demonstrates how to use the core transport APIs to send and receive messages.
*   When using the in-process transport, the client and server are running in the same process, so they can share the same memory. This means that there is no need to serialize and deserialize messages, which can lead to significant performance improvements.
