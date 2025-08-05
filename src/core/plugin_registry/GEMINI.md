# Plugin Registry

This directory contains the source code for the gRPC plugin registry.

See also: [gRPC Core overview](../GEMINI.md)

## Overarching Purpose

The plugin registry is responsible for initializing and configuring the gRPC core by registering a variety of plugins, including handshakers, transports, filters, and resolvers. It is the main entry point for configuring the gRPC core library, and it is called at startup to register all of the core plugins.

## Core Concepts

The plugin registry is built around the `CoreConfiguration::Builder` class, which is defined in the `core/config` directory. The `CoreConfiguration::Builder` provides a set of methods for registering different types of plugins. The plugin registry uses these methods to register all of the core plugins that are part of the gRPC library.

## Files

*   **`grpc_plugin_registry.cc`**: This is the core of the plugin registration system. It defines the `BuildCoreConfiguration` function, which is called at startup to register all the core plugins that are part of the gRPC library.
*   **`grpc_plugin_registry_extra.cc`**: This file registers "extra" filters and plugins, most notably those related to XDS (xDS is a protocol for dynamic resource discovery). These plugins are conditionally compiled, guarded by the `GRPC_NO_XDS` macro, so that they can be excluded from builds that don't need them.
*   **`grpc_plugin_registry_noextra.cc`**: This file provides an empty implementation of `RegisterExtraFilters`, which is used when the "extra" filters are not needed. This allows the core to be built without the XDS dependencies.

## Notes

*   The order of plugin registration matters, especially for handshakers. The TCP connect handshaker is intentionally registered last so that it's the first one to be tried.
*   The use of the `GRPC_NO_XDS` macro to control the inclusion of extra plugins is a key feature of this directory. It allows for a more lightweight build of gRPC when XDS is not needed.
*   The plugin registry is a good example of how gRPC uses a modular, plugin-based architecture. This architecture makes it easy to add new features to gRPC, and it allows for a high degree of customization.
