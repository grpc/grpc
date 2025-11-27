# Channel

This directory contains the core implementation of the gRPC channel stack.

## Overarching Purpose

The channel library provides the building blocks for creating and managing gRPC channels. It includes the implementation of the channel stack, which is a pipeline of filters that process incoming and outgoing call operations. The channel library is a low-level component of the gRPC stack, and it is used by both the client and server implementations.

## Core Concepts

The channel library is built around a few key abstractions:

*   **`ChannelStack`**: A `ChannelStack` is a pipeline of channel filters. Each filter in the stack can inspect and modify the call operations that pass through it. The channel stack is a key component of the gRPC architecture, and it provides a flexible and extensible way to add functionality to a channel.
*   **`ChannelStackBuilder`**: A `ChannelStackBuilder` is used to build a `ChannelStack`. It provides a convenient way to add filters to the stack and to configure the channel.
*   **`ChannelArgs`**: A `ChannelArgs` is an immutable set of key-value pairs that can be used to configure a channel. Channel args are used to control the behavior of the channel stack and its filters.
*   **`PromiseBasedFilter`**: A `PromiseBasedFilter` is a new type of channel filter that is based on the [gRPC Core Promise API](../../promise/GEMINI.md). Promise-based filters are designed to be more asynchronous and non-blocking than traditional, callback-based filters.

## Files

*   **`channel_args.h`, `channel_args.cc`**: These files define the `ChannelArgs` class.
*   **`channel_stack.h`, `channel_stack.cc`**: These files define the `ChannelStack` class.
*   **`channel_stack_builder.h`, `channel_stack_builder.cc`**: These files define the `ChannelStackBuilder` class.
*   **`promise_based_filter.h`, `promise_based_filter.cc`**: These files define the `PromiseBasedFilter` class.
*   **`connected_channel.h`, `connected_channel.cc`**: Defines a helper class for building channels that are based on a single transport.

## Notes

*   The channel stack is a powerful mechanism for adding functionality to a gRPC channel. It is used to implement a wide variety of features, including compression, error handling, and load balancing.
*   The channel library is in the process of being migrated to a new, promise-based architecture. As a result, you will see a mix of legacy callback-based code and newer promise-based code. The `PromiseBasedFilter` is a key part of this transition.
*   The channel library is a low-level component of the gRPC stack. Most application developers will not need to interact with it directly.
*   The channel stack is a linear pipeline of filters. This means that each filter in the stack has a single "next" filter that it can pass call operations to. This simple, linear design is one of the things that makes the channel stack so efficient and easy to reason about.
