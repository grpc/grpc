# "Chaotic Good" Transport (Legacy)

This directory contains a legacy implementation of the "Chaotic Good" transport.

See also: [gRPC Transports overview](../GEMINI.md)

## Overarching Purpose

This directory contains an older implementation of the "Chaotic Good" transport. It's currently the default used by many applications, and is being transitioned away from to the new implementation found in the [`chaotic_good`](../chaotic_good/GEMINI.md) directory.

## Files

*   `chaotic_good_transport.h`: This file contains the main entry points for the legacy Chaotic Good transport.
*   `client_transport.h`, `client_transport.cc`: These files contain the implementation of the client-side transport.
*   `server_transport.h`, `server_transport.cc`: These files contain the implementation of the server-side transport.
*   `frame.h`, `frame.cc`, `frame_header.h`, `frame_header.cc`: These files define the custom framing format used by the transport.

## Major Classes

*   `grpc_core::chaotic_good_legacy::ChaoticGoodTransport`: The main transport implementation.

## Notes

*   This implementation is considered obsolete and should not be used in new code.
