# DNS Name Resolution

This directory contains an implementation of name resolution that uses DNS.

## Overarching Purpose

This directory provides a concrete implementation of the `Resolver` interface that can be used to resolve names using DNS.

## Subdirectories

- **`c_ares/`**: An implementation of DNS resolution that uses the c-ares library.
- **`event_engine/`**: An implementation of DNS resolution that uses the EventEngine.
- **`native/`**: An implementation of DNS resolution that uses the native DNS resolver of the operating system.

## Notes

- This is the default name resolution mechanism for gRPC.
- It is well-tested and has been used in production for many years.
- The choice of which DNS resolver to use can have a significant impact on performance, so it is important to choose the right one for your application.
