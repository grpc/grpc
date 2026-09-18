# Endpoint Info

This directory contains the `EndpointInfo` class, which is a data structure that contains information about the local and remote endpoints of a connection.

## Overarching Purpose

The `EndpointInfo` class provides a convenient way to access information about a connection's endpoints, such as the local and remote addresses.

## Files

- **`endpoint_info.h`**: Defines the `EndpointInfo` class.

## Major Classes

- **`grpc_core::EndpointInfo`**: A data structure that contains information about the local and remote endpoints of a connection.

## Notes

- The `EndpointInfo` class is used by handshakers to access information about the connection they are operating on.
