# TCP Connect Handshaker

This directory contains a basic handshaker for TCP connections.

## Overarching Purpose

This handshaker is responsible for creating a TCP connection to a remote host. It is the simplest handshaker and is used as the base for more complex handshakers, such as the security handshakers.

## Files

- **`tcp_connect_handshaker.cc`**: The implementation of the TCP connect handshaker.

## Notes

- This handshaker does not provide any security features. It simply establishes a plain TCP connection.
- It is typically used as the first step in a handshaking process, followed by a security handshaker to establish a secure channel.
