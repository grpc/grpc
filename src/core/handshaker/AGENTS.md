# gRPC Handshaker

This directory contains the implementation of gRPC's handshaker framework, which is responsible for establishing a secure connection between a client and a server.

## Overarching Purpose

The handshaker framework provides a pluggable mechanism for performing various handshakes, such as TLS, ALTS, and HTTP CONNECT. It is designed to be extensible, allowing new handshaker implementations to be added easily.

## Files and Subdirectories

- **`handshaker.h` / `handshaker.cc`**: Defines the core `Handshaker` class, which represents a single handshaking operation.
- **`handshaker_factory.h`**: Defines the `HandshakerFactory` class, which is responsible for creating `Handshaker` instances.
- **`handshaker_registry.h` / `handshaker_registry.cc`**: Defines a registry for `HandshakerFactory` instances, allowing handshakers to be looked up by name.
- **`proxy_mapper.h`**: Defines the `ProxyMapper` interface, which is used to determine the proxy settings for a given target address.
- **`proxy_mapper_registry.h` / `proxy_mapper_registry.cc`**: Defines a registry for `ProxyMapper` instances.
- **`endpoint_info/`**: Contains the `EndpointInfo` class, which is a data structure that contains information about the local and remote endpoints of a connection.
- **`http_connect/`**: Contains the implementation of the HTTP CONNECT handshaker.
- **`security/`**: Contains the implementation of the security handshakers, which are responsible for performing TLS and ALTS handshakes.
- **`tcp_connect/`**: Contains a basic handshaker for TCP connections.

## Notes

- The handshaker framework is a critical component of gRPC's security infrastructure.
- It is responsible for ensuring that all connections are properly authenticated and encrypted.
- The framework is designed to be flexible and extensible, allowing it to be adapted to different security requirements.
