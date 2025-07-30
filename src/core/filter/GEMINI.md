# gRPC Core Filter

This directory contains the fundamental building blocks for the gRPC channel filter mechanism.

See also: [gRPC Core overview](../GEMINI.md)

## Overarching Purpose

The code in this directory provides the infrastructure for creating, composing, and managing channel filters. It defines the interfaces that filters must implement and provides the machinery for invoking them in the correct order. Filters are used to intercept and modify RPCs as they flow through the client and server channel stacks.

## Core Concepts

*   **`Blackboard`**: A `Blackboard` is a key-value store that allows filters to share state with each other. This is useful for filters that need to coordinate their behavior, but it should be used with care to avoid creating tight coupling between filters.
*   **`FilterArgs`**: The `FilterArgs` class provides arguments to filters that are independent of channel args. This includes things like the filter's instance ID and access to the blackboard.
*   **Fused Filters**: Fused filters are an optimization that allows multiple filters to be combined into a single filter. This can reduce the overhead of the filter chain, and it is particularly useful for filters that are very simple and have low overhead. This is an experimental feature.

## Files

*   **`blackboard.h`, `blackboard.cc`**: These files define the `Blackboard` class.
*   **`filter_args.h`**: Defines the `FilterArgs` class.
*   **`fused_filters.cc`**: Contains an optimization to fuse multiple filters together to reduce overhead.
*   **`auth/`**: This subdirectory contains authentication-related filters.

## Notes

*   The filter mechanism is a powerful tool for extending gRPC's functionality. It's used to implement features like authentication, retry, and compression.
*   Filters are arranged in a stack, and each filter can pass the RPC on to the next filter in the stack, or it can terminate the RPC.
*   Filters are used in both client and server channel stacks. See `../client_channel/GEMINI.md` and `../server/GEMINI.md` for more details about how filters are used in those stacks.
*   Filters are registered with the `CoreConfiguration`. See `../config/GEMINI.md` for more details about how the `CoreConfiguration` works.
