# Transport

This directory contains the core transport abstraction for gRPC.

## Overarching Purpose

The transport abstraction provides a unified interface for sending and receiving data over a network. It is responsible for things like flow control, multiplexing, and error handling.

## Files

- **`endpoint_transport.h`**: Defines the `grpc_endpoint_transport` interface, which is the core transport abstraction.
- **`auth_context.h` / `auth_context.cc`**: Defines the `grpc_auth_context` class, which is used to represent the security context of a connection.
- **`endpoint_transport_client_channel_factory.h` / `endpoint_transport_client_channel_factory.cc`**: Defines a factory for creating client channels that are backed by an `endpoint_transport`.

## Notes

- The transport abstraction is a key component of gRPC's networking infrastructure.
- It is responsible for providing a reliable and efficient way to send and receive data over a network.
- The `grpc_endpoint_transport` interface is designed to be extensible, allowing new transport implementations to be added easily.
