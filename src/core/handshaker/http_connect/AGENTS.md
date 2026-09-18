# HTTP CONNECT Handshaker

This directory contains the implementation of the HTTP CONNECT handshaker.

## Overarching Purpose

The HTTP CONNECT handshaker is responsible for establishing a connection to a target server through an HTTP proxy.

## Files

- **`http_connect_handshaker.cc`**: The implementation of the HTTP CONNECT handshaker.

## Notes

- The HTTP CONNECT handshaker is used when gRPC needs to connect to a server through an HTTP proxy that supports the CONNECT method.
- This handshaker is typically used in conjunction with a security handshaker, such as the TLS handshaker, to establish a secure connection to the target server.
