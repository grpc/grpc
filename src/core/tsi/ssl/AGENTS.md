# SSL TSI Implementation

This directory contains an implementation of the Transport Security Interface (TSI) that uses OpenSSL.

## Overarching Purpose

This directory provides a concrete implementation of the TSI interfaces that can be used to secure gRPC connections with TLS.

## Subdirectories

- **`key_logging/`**: Contains code for logging SSL keys, which can be useful for debugging.
- **`session_cache/`**: Contains a cache for SSL sessions, which can be used to speed up reconnections.

## Notes

- This is the default TSI implementation for gRPC.
- It is well-tested and has been used in production for many years.
- It is important to keep OpenSSL up-to-date to ensure that your gRPC applications are secure.
