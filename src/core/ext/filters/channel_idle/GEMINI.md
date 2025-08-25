# Channel Idle and Max Age Filters

This directory contains filters for managing the lifecycle of a gRPC channel based on idleness and maximum connection age.

See also: [gRPC Filters overview](../../filter/GEMINI.md)

## Overarching Purpose

These filters provide mechanisms to automatically close connections that are either idle for too long or have been active for a maximum allowed duration. This helps with resource management and can prevent connections from becoming stale.

## Files

*   `legacy_channel_idle_filter.h`, `legacy_channel_idle_filter.cc`: These files implement the `LegacyChannelIdleFilter` class, which is a base class for the following filters:
    *   `LegacyClientIdleFilter`: A client-side filter that closes the transport if the channel has been idle (i.e., has no active calls) for a configurable amount of time. This is controlled by the `GRPC_ARG_CLIENT_IDLE_TIMEOUT_MS` channel argument.
    *   `LegacyMaxAgeFilter`: A server-side filter that enforces a maximum connection age. It sends a `GOAWAY` frame when the connection reaches its maximum age and, after a grace period, closes the connection. It also supports a maximum idle time. These are configured via the `GRPC_ARG_MAX_CONNECTION_AGE_MS`, `GRPC_ARG_MAX_CONNECTION_IDLE_MS`, and `GRPC_ARG_MAX_CONNECTION_AGE_GRACE_MS` channel arguments.
*   `idle_filter_state.h`, `idle_filter_state.cc`: These files define the `IdleFilterState` class, which encapsulates the state machine for tracking the number of active calls on a channel and determining if the idle timer should be active.

## Major Classes

*   `grpc_core::LegacyChannelIdleFilter`: A base class for the `LegacyClientIdleFilter` and `LegacyMaxAgeFilter` classes. It provides the common functionality for managing the idle state of a channel. It is not a channel filter itself.
*   `grpc_core::LegacyClientIdleFilter`: A client-side filter for closing idle connections.
*   `grpc_core::LegacyMaxAgeFilter`: A server-side filter for enforcing maximum connection age.
*   `grpc_core::IdleFilterState`: A helper class that implements a state machine for managing the idle state of a channel. It tracks the number of active calls and whether an idle timer is active.

## Notes

*   The "legacy" prefix in the class names indicates that these filters are based on the older channel filter API. They will likely be replaced by a more modern implementation in the future.
*   These filters are crucial for maintaining healthy and efficient connections in long-running gRPC applications. They are configured via channel arguments, which can be set when the channel is created.
