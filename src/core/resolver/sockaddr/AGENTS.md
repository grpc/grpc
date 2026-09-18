# Sockaddr Name Resolution

This directory contains an implementation of name resolution that uses a list of socket addresses.

## Overarching Purpose

This directory provides a concrete implementation of the `Resolver` interface that can be used to resolve names from a list of socket addresses. This is useful for testing and for cases where the list of addresses is known in advance.

## Files

- **`sockaddr_resolver.h` / `sockaddr_resolver.cc`**: The implementation of the sockaddr resolver.

## Notes

- This is a simple but useful implementation of name resolution.
- It can be used to test gRPC applications without having to set up a DNS server.
- It can also be used in production for cases where the list of addresses is static and known in advance.
