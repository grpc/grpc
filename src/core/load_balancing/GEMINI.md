# gRPC Load Balancing

This directory contains the implementation of gRPC's load balancing framework.

See also: [gRPC Client Channel overview](../client_channel/GEMINI.md)

## Overarching Purpose

The code in this directory provides a flexible and extensible framework for load balancing. It allows different load balancing policies to be plugged in to gRPC to control how RPCs are distributed across a set of backend servers.

## Core Concepts

The load balancing framework is built around a few key abstractions:

*   **`LoadBalancingPolicy`**: The `LoadBalancingPolicy` is the main abstraction for load balancing in gRPC. It is responsible for creating and managing subchannels, and for deciding which subchannel to use for each RPC.
*   **`SubchannelPicker`**: The `SubchannelPicker` is a simple interface that is used to select a subchannel for a given RPC. Each `LoadBalancingPolicy` has a `SubchannelPicker` that is used to make the final routing decision.
*   **`LoadBalancingPolicyFactory`**: A `LoadBalancingPolicyFactory` is responsible for creating instances of a particular `LoadBalancingPolicy`.
*   **`LoadBalancingPolicyRegistry`**: The `LoadBalancingPolicyRegistry` is a global registry of `LoadBalancingPolicyFactory` instances. This registry is used to create `LoadBalancingPolicy` instances by name.

## Load Balancing Policies

gRPC comes with a number of built-in load balancing policies, each of which is implemented in its own subdirectory:

*   **`pick_first`**: This policy tries to connect to the first address in the list, and if that fails, it tries the next one, and so on. It's the default policy if no other is specified.
*   **`round_robin`**: This policy distributes RPCs across all available backend connections in a round-robin fashion.
*   **`weighted_round_robin`**: A more advanced version of round-robin that takes into account the weights of the different backends.
*   **`ring_hash`**: This policy uses a consistent hashing algorithm to distribute RPCs across the backends. This is useful for session affinity, as it ensures that all RPCs for a given session are sent to the same backend.
*   **`grpclb`**: This policy uses an external load balancer to make load balancing decisions. The external load balancer is typically a separate process that is running on the same machine as the gRPC client.
*   **`xds`**: This policy uses the xDS protocol to configure load balancing. xDS is a set of APIs that are used to configure service discovery, load balancing, and other features in a service mesh. See the [xDS documentation](../xds/GEMINI.md) for more details.
*   **`rls`**: Route Lookup Service. This policy uses a separate service to determine the route for each RPC.

## Files

*   **`lb_policy.h`, `lb_policy.cc`**: These files define the core `LoadBalancingPolicy` interface.
*   **`lb_policy_registry.h`, `lb_policy_registry.cc`**: These files define the `LoadBalancingPolicyRegistry`.
*   **`subchannel_interface.h`**: Defines the interface that a subchannel must implement to be used by a `LoadBalancingPolicy`.
*   **`child_policy_handler.h`, `child_policy_handler.cc`**: A helper class for implementing composite load balancing policies that delegate to child policies. This is used by policies like `xds` and `priority`.
*   **`address_filtering.h`, `address_filtering.cc`**: A helper class for filtering addresses based on a set of attributes. This is used by policies like `xds` to implement locality-based routing.
*   **`health_check_client.h`, `health_check_client.cc`**: A client for the gRPC health checking protocol. This is used by some LB policies to determine the health of the backends.

## Notes

*   gRPC's load balancing framework is highly extensible. New load balancing policies can be added by implementing the `LoadBalancingPolicy` and `LoadBalancingPolicyFactory` interfaces and registering the factory with the `LoadBalancingPolicyRegistry`.
*   The load balancing policy is chosen based on the service config, which can be provided by the [resolver](../resolver/GEMINI.md).
*   Load balancing is primarily used in the client channel. See the [client channel documentation](../client_channel/GEMINI.md) for more details.
