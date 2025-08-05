# gRPC Service Config

This directory contains the implementation of gRPC's service config feature, which allows for per-service and per-method configuration of a gRPC channel.

See also: [gRPC Core overview](../GEMINI.md)

## Overarching Purpose

The code in this directory provides a mechanism for parsing and applying service configs. Service configs can be used to configure a variety of features, such as load balancing, retries, and message size limits.

## Core Concepts

*   **`ServiceConfig`**: The `ServiceConfig` class is an abstract base class for service configs. It provides methods for retrieving the raw JSON string of the service config, as well as for retrieving parsed config values for a given method.
*   **`ServiceConfigParser`**: The `ServiceConfigParser` class is a registry of `Parser` instances. Each `Parser` is responsible for parsing a specific part of the service config. This makes the service config framework highly extensible.

## How to Add a New Service Config Parser

1.  Create a new class that derives from `ServiceConfigParser::Parser`.
2.  Implement the `ParseGlobalParams` and/or `ParsePerMethodParams` methods. These methods will be called by the `ServiceConfig` framework to parse the global and per-method parts of the service config, respectively.
3.  Register your parser with the `CoreConfiguration` builder at startup.

## Files

*   **`service_config.h`**: Defines the `ServiceConfig` class.
*   **`service_config_parser.h`, `service_config_parser.cc`**: These files define the `ServiceConfigParser` class.
*   **`service_config_impl.h`, `service_config_impl.cc`**: These files define the `ServiceConfigImpl` class, which is the concrete implementation of the `ServiceConfig` interface.
*   **`service_config_call_data.h`**: Defines the `ServiceConfigCallData` class, which is used to store the per-call service config data.

## Notes

*   Service configs are typically provided by the name resolver (see [`../resolver/GEMINI.md`](../resolver/GEMINI.md)).
*   Service configs can also be delivered via xDS (see [`../xds/GEMINI.md`](../xds/GEMINI.md)).
*   The service config is a powerful feature that allows for a great deal of flexibility in configuring gRPC channels.
*   The service config is used to configure load balancing (see [`../load_balancing/GEMINI.md`](../load_balancing/GEMINI.md)).
