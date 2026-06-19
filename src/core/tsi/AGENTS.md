# Transport Security Interface (TSI)

This directory contains the implementation of gRPC's Transport Security Interface (TSI), which is an abstraction for different transport security mechanisms like TLS and ALTS.

## Overarching Purpose

TSI provides a unified interface for performing transport-level security handshakes and protecting data. This allows gRPC to support different security mechanisms without having to change the core transport code.

## Files and Subdirectories

- **`transport_security_interface.h`**: Defines the core `tsi_handshaker` and `tsi_frame_protector` interfaces.
- **`transport_security.h` / `transport_security.cc`**: Defines the `tsi_transport_security` interface, which is a higher-level abstraction that combines the `tsi_handshaker` and `tsi_frame_protector` interfaces.
- **`transport_security_grpc.h` / `transport_security_grpc.cc`**: Provides a gRPC-specific implementation of the `tsi_transport_security` interface.
- **`ssl/`**: An implementation of TSI that uses OpenSSL.
- **`alts/`**: An implementation of TSI that uses ALTS.
- **`fake_transport_security.h` / `fake_transport_security.cc`**: A fake implementation of TSI for testing.
- **`local_transport_security.h` / `local_transport_security.cc`**: An implementation of TSI for local (e.g., UDS) connections.

## Notes

- TSI is a key component of gRPC's security infrastructure.
- It provides a flexible and extensible way to add new security mechanisms to gRPC.
- The TSI interfaces are designed to be simple and easy to implement, which makes it easy to add support for new security mechanisms.
