# gRPC Client Channel

This directory contains the core implementation of the client-side channel in gRPC. It's responsible for managing the entire lifecycle of a connection from a client to a server, including name resolution, load balancing, and connectivity.

## Overarching Purpose

The code in this directory provides the building blocks for creating and managing client-side channels. It orchestrates the process of turning a target URI into a set of connections to backend servers, and then distributing RPCs across those connections.

## Core Concepts

The client channel is built around a few key abstractions:

*   **`ClientChannel`**: The top-level object that represents a connection to a service. It owns the other components and is responsible for orchestrating the overall process of name resolution, load balancing, and connection management.
*   **`Subchannel`**: A `Subchannel` represents a connection to a single backend address. A `ClientChannel` typically manages multiple `Subchannel`s, one for each address returned by the resolver.
*   **`Resolver`**: The `Resolver` is responsible for taking a target URI (e.g., "dns:///my-service.example.com") and resolving it to a list of backend addresses. See the [resolver documentation](../resolver/GEMINI.md) for more details.
*   **`LoadBalancingPolicy`**: The `LoadBalancingPolicy` is responsible for taking the list of subchannels from the `ClientChannel` and deciding which one to use for each RPC. See the [load balancing documentation](../load_balancing/GEMINI.md) for more details.

## Legacy Architecture

The original architecture of the client channel relied heavily on callback-based filters. This approach is still present in some parts of the code, but is being actively migrated to a more modern, promise-based model.

*   **`retry_filter.h`, `retry_filter.cc`**: Implements a channel filter that provides automatic retries for failed RPCs, based on the service config. This is an example of a "dynamic filter", which can be configured on a per-service or per-method basis.
*   **`dynamic_filters.h`, `dynamic_filters.cc`**: These files provide the framework for creating and managing dynamic filters.

## Modern Architecture

The newer architecture of the client channel is based on promise-based interceptors. This approach is more flexible and easier to reason about, and is the preferred way to implement new features.

*   **`retry_interceptor.h`, `retry_interceptor.cc`**: Implements an interceptor that provides automatic retries for failed RPCs, based on the service config. This is the modern, promise-based replacement for `retry_filter`.

## Files

*   **`client_channel.h`, `client_channel.cc`**: These files contain the main `ClientChannel` class, which is the primary implementation of the `grpc::Channel` interface for clients. It manages the entire lifecycle of a client-side channel.
*   **`subchannel.h`, `subchannel.cc`**: These files define the `Subchannel` class, which represents a connection to a single backend address.
*   **`client_channel_factory.h`, `client_channel_factory.cc`**: These files define the `ClientChannelFactory`, which is responsible for creating `ClientChannel` and `Subchannel` instances.
*   **`connector.h`**: Defines the `Connector` interface, which is responsible for establishing a transport-level connection to a given address.
*   **`load_balanced_call_destination.h`, `load_balanced_call_destination.cc`**: Defines the `LoadBalancedCallDestination`, which is a call destination that uses a `LoadBalancingPolicy` to select a subchannel for each call.
*   **`retry_filter.h`, `retry_filter.cc`**: Implements a channel filter that provides automatic retries for failed RPCs, based on the service config. This is an example of a "dynamic filter", which can be configured on a per-service or per-method basis.
*   **`retry_interceptor.h`, `retry_interceptor.cc`**: Implements an interceptor that provides automatic retries for failed RPCs, based on the service config.
*   **`config_selector.h`**: Defines the `ConfigSelector` class, which is used to select the appropriate service config for a given RPC.
*   **`subchannel_pool_interface.h`, `subchannel_pool_interface.cc`**: Defines the interface for a subchannel pool, which is used to cache and reuse subchannels.
*   **`global_subchannel_pool.h`, `global_subchannel_pool.cc`**: Implements a global subchannel pool that can be shared across multiple channels.

## Major Classes

*   **`grpc_core::ClientChannel`**: The main client-side channel implementation. It integrates the [resolver](../resolver/GEMINI.md), [load balancing policy](../load_balancing/GEMINI.md), and subchannels to provide a unified view of a connection to a service.
*   **`grpc_core::Subchannel`**: Represents a single connection to a backend. It manages the connectivity state of that connection and provides a mechanism for creating transports.
*   **`grpc_core::ClientChannelFactory`**: A factory class for creating client channels and subchannels.
*   **`grpc_core::Connector`**: An interface for creating a transport-level connection.

## Notes

*   The client channel is a complex piece of machinery that coordinates several different components. Understanding the roles of the [resolver](../resolver/GEMINI.md), [load balancing policy](../load_balancing/GEMINI.md), and subchannels is key to understanding how it works.
*   Much of the client channel is being migrated to a new, promise-based architecture. As a result, you will see a mix of legacy callback-based code and newer promise-based code. The `retry_filter.h` and `retry_interceptor.h` files are a good example of this transition, where the former is the legacy implementation and the latter is the new promise-based one.
*   The `dynamic_filters` are a legacy mechanism for adding filters to the channel. They are being replaced by the more flexible interceptor model.
*   The `ClientChannel` uses a `WorkSerializer` to ensure that all of its internal state is accessed in a thread-safe manner.
*   The `ConfigSelector` allows for different service configs to be used for different RPCs on the same channel. This is used to support features like per-method retry policies.
