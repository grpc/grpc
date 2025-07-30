# "Chaotic Good" Transport

This directory contains the implementation of a new, experimental gRPC transport called "Chaotic Good".

See also: [gRPC Transports overview](../transport/GEMINI.md)

## Overarching Purpose

The "Chaotic Good" transport is a new transport implementation for gRPC that is designed to be more performant and flexible than the existing CHTTP2 transport. It is based on a custom framing format and is heavily reliant on the gRPC Core Promise API.

See also: [gRPC Transports directory](../)
## Core Concepts

The "Chaotic Good" transport is built around a few key concepts:

*   **Custom Framing Format**: The transport uses a custom framing format that is defined in the `chaotic_good_frame.proto` file. The framing format is designed to be simple and efficient, and it supports all of the features of gRPC, including headers, messages, and trailers.
*   **Promise-Based Architecture**: The transport is heavily based on the [gRPC Core Promise API](../../lib/promise/GEMINI.md). This allows the transport to be implemented in a more asynchronous and non-blocking way, which can lead to better performance and scalability.
*   **Separation of Control and Data Planes**: The transport separates the control plane (e.g., sending and receiving headers) from the data plane (e.g., sending and receiving messages). This allows the transport to be more flexible and to support a variety of different use cases.

## Files

*   **`chaotic_good.h`, `chaotic_good.cc`**: These files contain the main entry points for the Chaotic Good transport.
*   **`client_transport.h`, `client_transport.cc`**: These files contain the implementation of the client-side transport.
*   **`server_transport.h`, `server_transport.cc`**: These files contain the implementation of the server-side transport.
*   **`frame.h`, `frame.cc`**: These files define the C++ classes that represent the different types of frames in the custom framing format.
*   **`frame_header.h`, `frame_header.cc`**: These files define the header for each frame in the custom framing format.
*   **`chaotic_good_frame.proto`**: This file contains the protobuf definition of the framing format.
*   **`frame_transport.h`**: Defines the interface for a transport that can send and receive frames.
*   **`control_endpoint.h`, `control_endpoint.cc`**: Implements the control plane for the transport.
*   **`data_endpoints.h`, `data_endpoints.cc`**: Implements the data plane for the transport.
*   **`scheduler.h`, `scheduler.cc`**: A simple scheduler for running promises.

## Major Classes

*   `grpc_core::chaotic_good::ChaoticGoodClientTransport`: The client-side transport implementation.
*   `grpc_core::chaotic_good::ChaoticGoodServerTransport`: The server-side transport implementation.
*   `grpc_core::chaotic_good::FrameInterface`: The base class for all frame types.
*   `grpc_core::chaotic_good::FrameTransport`: The interface for a transport that can send and receive frames.

## Notes

*   This transport is still under development and is not yet recommended for production use.
*   The name "Chaotic Good" is a reference to the Dungeons & Dragons alignment system. It originally reflected that Chaotic Good was a transport with an alignment story, as it guaranteed alignment for received messages.
*   The transport is a good example of how the gRPC Core Promise API can be used to build high-performance, asynchronous network applications.
*   The separation of the control and data planes is a key design feature of the transport. It allows the transport to be more flexible and to support a variety of different use cases. For example, the control plane could be implemented over a reliable transport like TCP, while the data plane could be implemented over a less reliable transport like UDP.
