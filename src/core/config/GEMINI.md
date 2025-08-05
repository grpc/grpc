# gRPC Core Configuration

This directory contains the fundamental building blocks for configuring the gRPC core library.

See also: [gRPC Core overview](../GEMINI.md)

## Overarching Purpose

This directory manages both static configuration (via config variables) and dynamic configuration (via pluggable components). It provides a centralized and extensible way to configure the behavior of the gRPC core library.

## Core Concepts

*   **`ConfigVars`**: `ConfigVars` is a singleton that holds configuration properties for gRPC. These properties are loaded from environment variables, Abseil flags, and can be programmatically overridden. This provides a unified way to access configuration values throughout the library. The values are automatically generated from `config_vars.yaml`.
*   **`CoreConfiguration`**: `CoreConfiguration` is a singleton that acts as a central registry for all pluggable components in gRPC. It uses a builder pattern (`CoreConfiguration::Builder`) to allow different parts of the system to register factories for things like resolvers, load balancing policies, handshakers, and channel filters. This is the primary mechanism for extending gRPC's functionality.

## Files

*   **`config_vars.h`, `config_vars.cc`**: These files define the `ConfigVars` class.
*   **`config_vars.yaml`**: This file contains the definitions of all of the configuration variables that are available in gRPC. The `config_vars.h` and `config_vars.cc` files are generated from this file.
*   **`core_configuration.h`, `core_configuration.cc`**: These files define the `CoreConfiguration` class and its builder.
*   **`load_config.h`, `load_config.cc`**: These files provide helper functions to load configuration values from various sources (flags, environment variables, and programmatic overrides) with a defined order of precedence.

## Major Classes

*   **`grpc_core::ConfigVars`**: A singleton class that provides access to gRPC's core configuration variables. It's initialized once and can be accessed via `ConfigVars::Get()`.
*   **`grpc_core::CoreConfiguration`**: A singleton that holds registries for various pluggable components. It's constructed via a `Builder` pattern, and is the main extension point for adding functionality to gRPC.
*   **`grpc_core::CoreConfiguration::Builder`**: A builder class that is used to construct the `CoreConfiguration` singleton. It provides methods for registering factories for the different types of pluggable components.

## Notes

*   The configuration system is designed to be highly extensible. The `CoreConfiguration` class and its builder pattern are key to this design, allowing for a la carte inclusion of features.
*   The `ConfigVars` are a good example of how gRPC uses code generation to ensure a single source of truth for configuration variables.
*   The `CoreConfiguration` is initialized at startup, and it can be customized by registering new factories with the builder. This is the primary mechanism for extending gRPC's functionality. For example, to add a new load balancing policy, you would implement the `LoadBalancingPolicyFactory` interface and then register your factory with the `CoreConfiguration::Builder`.
