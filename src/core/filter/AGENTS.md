# gRPC Core Filter

This directory contains filter configuration types, chain builders, and specific core filters.

See also: [gRPC Core overview](../AGENTS.md)

## Keep This Document Current

**Keep this document in sync with the code it describes.**

*   **Trigger:** Your CL modifies files, classes, functions, or experiments referenced here.
*   **Action:** Update all references, lists, and descriptions in the same CL.
*   **Verify:** Re-check every list and symbol against the source before finishing.

## Overarching Purpose

*   Provides filter configuration (`FilterConfig`, `FilterArgs`) and chain-building abstractions (`FilterChain`, `FilterChainBuilder`).
*   Registers fused filter combinations (`RegisterFusedFilters`) for common client and server stacks.
*   Implements authentication (`auth/`), composite (`composite/`), and external processing (`ext_proc/`) filters.
*   Core filter interfaces and stack machinery live in [lib/channel](../lib/channel/AGENTS.md) and [call](../call/AGENTS.md).

## Core Concepts

*   **`FilterConfig`**: Ref-counted base class for per-filter configuration objects.
*   **`FilterArgs`**: Passes per-instance arguments (`instance_id()`, `config()`) independent of `ChannelArgs`.
*   **`FilterChain` / `FilterChainBuilder`**: Abstracts filter chain construction across V1 and V3 stacks.
*   **Fused Filters**: Combines multiple filters into one to reduce call-stack overhead.
    *   The `FusedFilter` template is defined in `src/core/call/filter_fusion.h`.
    *   `FusedFilter` derives from `ImplementChannelFilter` (`lib/channel/promise_based_filter.h`).
    *   `fused_filters.cc` registers concrete fused combinations when the `fuse_filters` experiment is enabled.
    *   `fused_filters.cc` compiles to a no-op when `GRPC_NO_FILTER_FUSION` is defined.
    *   `ChannelInit::CreateStack` merges fused filters into legacy (V1) stacks only.
    *   The V3 `InterceptionChainBuilder` path does not apply fusions.

## Files

*   **`filter_args.h`**: Defines `FilterConfig`, `FilterAndConfig`, `FilterArgs`, `GRPC_ARG_USE_V3_STACK`, and `GRPC_ARG_IS_SERVER_FILTER_STACK`.
*   **`filter_chain.h`**: Defines `FilterChain` and `FilterChainBuilder`.
*   **`fused_filters.cc`**: Defines and registers fused filter combinations via `RegisterFusedFilters`.
*   **`auth/`**: Implements `ClientAuthFilter` and `ServerAuthFilter` (`auth_filters.h`, `client_auth_filter.cc`, `server_auth_filter.cc`).
*   **`composite/`**: Implements `CompositeFilter` for xDS matcher-driven filter execution (`composite_filter.{h,cc}`).
*   **`ext_proc/`**: Implements `ExtProcFilter` for xDS external processing (`ext_proc_filter.{h,cc}`, `ext_proc_messages.{h,cc}`).

## Notes

*   The filter mechanism is a powerful tool for extending gRPC's functionality. It's used to implement features like authentication, retry, and compression.
*   Legacy stack and promise filter interfaces live in [lib/channel](../lib/channel/AGENTS.md) (`channel_stack.h`, `promise_based_filter.h`).
*   Call V3 filter execution lives in [call](../call/AGENTS.md) (`call_filters.h`, `interception_chain.h`).
*   Most other concrete filters live under `src/core/ext/filters/`.
*   Client and server stack usage is covered in [client_channel](../client_channel/AGENTS.md) and [server](../server/AGENTS.md).
*   Filters are registered via `CoreConfiguration::Builder::channel_init()`; see [config](../config/AGENTS.md).
