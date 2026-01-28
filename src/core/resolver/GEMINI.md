# Name Resolution

This directory contains the implementation of gRPC's name resolution framework.

## Overarching Purpose

The name resolution framework provides a pluggable mechanism for resolving a logical name into a list of network addresses. This is a key component of gRPC's load balancing and failover systems.

## Files and Subdirectories

- **`resolver.h` / `resolver.cc`**: Defines the core `Resolver` class, which is the main interface for name resolution.
- **`resolver_factory.h`**: Defines the `ResolverFactory` class, which is used to create `Resolver` instances.
- **`resolver_registry.h` / `resolver_registry.cc`**: Defines a registry for `ResolverFactory` instances.
- **`dns/`**: An implementation of name resolution that uses DNS.
- **`fake/`**: A fake implementation of name resolution for testing.
- **`google_c2p/`**: An implementation of name resolution that is specific to Google's C2P infrastructure.
- **`sockaddr/`**: An implementation of name resolution that uses a list of socket addresses.
- **`xds/`**: An implementation of name resolution that uses XDS.

## Notes

- The name resolution framework is a key component of gRPC's networking infrastructure.
- It provides a flexible and extensible way to add new name resolution mechanisms to gRPC.
- The `Resolver` interface is designed to be simple and easy to implement, which makes it easy to add support for new name resolution mechanisms.
