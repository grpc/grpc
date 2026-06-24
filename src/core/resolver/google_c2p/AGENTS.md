# Google C2P Name Resolution

This directory contains an implementation of name resolution that is specific to Google's C2P infrastructure.

## Overarching Purpose

This directory provides a concrete implementation of the `Resolver` interface that can be used to resolve names in Google's C2P infrastructure.

## Files

- **`google_c2p_resolver.h` / `google_c2p_resolver.cc`**: The implementation of the Google C2P resolver.

## Notes

- This is a Google-specific implementation of name resolution.
- It is designed to be used by gRPC applications that run within Google's C2P infrastructure.
- It is not intended for use outside of Google.
