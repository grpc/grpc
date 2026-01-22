# gRPC Core Call

This directory is the heart of the gRPC C++ core, defining the fundamental data structures and mechanisms for representing and managing a single RPC.

See also: [gRPC Core overview](../GEMINI.md)

## Overarching Purpose

The code in this directory provides the client and server-side representations of a call, the central "spine" that connects them, and the various components needed to manage the lifecycle of an RPC, including metadata, messages, and status.

## Core Concepts

*   **`CallSpine`**: The `CallSpine` is the central component of a gRPC call. It encapsulates the call's context, including the arena allocator, call filters, and the pipes for message and metadata communication. The `CallSpine` is shared between a `CallInitiator` (the client-side) and a `CallHandler` (the server-side).
*   **`CallInitiator`**: The `CallInitiator` is the client-side view of a call. It is used for initiating requests and receiving responses.
*   **`CallHandler`**: The `CallHandler` is the server-side view of a call. It is used for handling incoming requests and sending responses.
*   **`ClientCall`**: The `ClientCall` is the public client-side API for a call. It wraps the `CallInitiator` and `CallSpine`.
*   **`ServerCall`**: The `ServerCall` is the public server-side API for a call. It wraps the `CallHandler` and `CallSpine`.

## Files

*   **`call_spine.h`, `call_spine.cc`**: These files define the `CallSpine` class.
*   **`client_call.h`, `client_call.cc`**: These files define the `ClientCall` class.
*   **`server_call.h`, `server_call.cc`**: These files define the `ServerCall` class.
*   **`metadata.h`, `metadata_batch.h`**: These files provide the data structures for representing and manipulating RPC metadata.
*   **`message.h`**: Defines the `Message` class, which is a container for RPC messages.
*   **`call_filters.h`, `call_filters.cc`**: These files define the `CallFilters` class, which is responsible for managing the filters for a call.
*   **`interception_chain.h`, `interception_chain.cc`**: These files define the `InterceptionChain` class, which is used to manage the execution of a set of interceptors.

## Notes

*   The `CallSpine` is the key abstraction to understand in this directory. It's the "glue" that holds a call together.
*   The use of `CallInitiator` and `CallHandler` provides a clean separation of concerns between the client and server sides of a call.
*   This directory is heavily based on the [gRPC Core Promise API](../lib/promise/GEMINI.md). Familiarity with that API is essential for understanding the code here.
*   The `CallFilters` class is responsible for managing the filters for a call. See the [channel documentation](../lib/channel/GEMINI.md) for more information about filters.
