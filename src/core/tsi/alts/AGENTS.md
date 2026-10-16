# ALTS TSI Implementation

This directory contains an implementation of the Transport Security Interface (TSI) that uses Application Layer Transport Security (ALTS).

## Overarching Purpose

This directory provides a concrete implementation of the TSI interfaces that can be used to secure gRPC connections with ALTS. ALTS is a mutual authentication and transport encryption system developed by Google.

## Subdirectories

- **`crypt/`**: Contains code for performing cryptographic operations.
- **`frame_protector/`**: Contains the implementation of the `tsi_frame_proteator` interface for ALTS.
- **`handshaker/`**: Contains the implementation of the `tsi_handshaker` interface for ALTS.
- **`zero_copy_frame_protector/`**: Contains an implementation of the `tsi_frame_protector` interface that avoids copying data.

## Notes

- ALTS is a Google-specific security protocol.
- It is designed to be efficient and to provide strong security for connections between services running within Google's infrastructure.
- The ALTS TSI implementation is used by gRPC applications that need to communicate with other services that use ALTS.
