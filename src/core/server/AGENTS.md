# gRPC Server

This directory contains the core implementation of the gRPC server.

## Overarching Purpose

The gRPC server is responsible for listening for incoming connections, handling requests, and sending responses. It is the main entry point for all server-side gRPC applications.

## Files

- **`server.h` / `server.cc`**: Defines the `Server` class, which is the main class for the gRPC server.
- **`server_interface.h`**: Defines the `ServerInterface` class, which is an interface that can be used to interact with the server.
- **`add_port.cc`**: Contains the implementation of the `grpc_server_add_http2_port` function, which is used to add a listening port to the server.
- **`server_call_tracer_filter.h` / `server_call_tracer_filter.cc`**: Defines a filter that can be used to trace server-side calls.
- **`server_config_selector.h`**: Defines the `ServerConfigSelector` class, which is used to select the server configuration for a given request.
- **`server_config_selector_filter.h` / `server_config_selector_filter.cc`**: Defines a filter that uses the `ServerConfigSelector` to select the server configuration.
- **`xds_channel_stack_modifier.h` / `xds_channel_stack_modifier.cc`**: Defines a class that can be used to modify the channel stack for XDS-enabled servers.
- **`xds_server_config_fetcher.cc`**: Fetches server configuration from an XDS server.

## Major Classes

- **`grpc_core::Server`**: The main class for the gRPC server.
- **`grpc_core::ServerInterface`**: An interface that can be used to interact with the server.
- **`grpc_core::ServerConfigSelector`**: A class that is used to select the server configuration for a given request.

## Notes

- The gRPC server is a complex piece of software with many different components.
- The `Server` class is the main entry point for all server-side gRPC applications.
- The `ServerBuilder` class can be used to create and configure a `Server` instance.
- The server can be configured with a variety of options, including the number of threads, the maximum number of concurrent requests, and the security credentials to use.
