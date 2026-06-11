# Security Handshakers

This directory contains the implementation of the security handshakers, which are responsible for performing TLS and ALTS handshakes.

## Overarching Purpose

The security handshakers are responsible for establishing a secure connection between a client and a server. They use a transport security mechanism, such as TLS or ALTS, to authenticate the server and encrypt all communication between the client and the server.

## Files

- **`security_handshaker.h` / `security_handshaker.cc`**: Defines the `SecurityHandshaker` class, which is the base class for all security handshakers.
- **`secure_endpoint.h` / `secure_endpoint.cc`**: Defines the `SecureEndpoint` class, which is a wrapper around a `grpc_endpoint` that provides a secure channel.
- **`pipelined_secure_endpoint.cc`**: A `SecureEndpoint` implementation that supports pipelining.

## Notes

- The security handshakers are a critical component of gRPC's security infrastructure.
- They are responsible for ensuring that all connections are properly authenticated and encrypted.
- The `SecurityHandshaker` class is designed to be extensible, allowing new security mechanisms to be added easily.
