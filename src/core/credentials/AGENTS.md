# gRPC Credentials

This directory contains the core implementation of gRPC's credential system.

## Overarching Purpose

The credential system provides a pluggable mechanism for authenticating and authorizing gRPC calls. It is designed to be extensible, allowing new credential types to be added easily.

## Subdirectories

- **`call/`**: Defines the `grpc_call_credentials` interface, which is used to add per-call security information to a gRPC call.
- **`transport/`**: Contains credentials implementations that are tied to a specific transport security type.

## Notes

- The credential system is a key component of gRPC's security infrastructure.
- It is responsible for ensuring that all gRPC calls are properly authenticated and authorized.
- The framework is designed to be flexible and extensible, allowing it to be adapted to different security requirements.
