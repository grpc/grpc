# CHTTP2 Transport

This directory contains the implementation of the CHTTP2 transport, which is the primary transport used in gRPC.

See also: [gRPC Transports overview](../GEMINI.md)

## Overarching Purpose

The CHTTP2 transport is a highly optimized and feature-rich implementation of HTTP/2 that is used for both client and server communication. It is responsible for managing the HTTP/2 connection, including stream multiplexing, flow control, and header compression.

## Core Concepts

*   **HPACK**: The CHTTP2 transport includes a full implementation of the [HPACK](https://httpwg.org/specs/rfc7541.html) header compression algorithm. This is used to compress the headers of gRPC requests and responses, which can significantly reduce the amount of data that needs to be sent over the network.
*   **Flow Control**: The transport implements both stream-level and connection-level flow control. This is used to prevent a single stream or connection from consuming too much memory, and to ensure that all streams get a fair share of the available bandwidth.
*   **Stream Multiplexing**: The transport supports multiplexing multiple gRPC streams over a single TCP connection. This is a key feature of HTTP/2, and it allows gRPC to achieve high performance and low latency.

## Subdirectories

*   **`alpn`**: Contains code for ALPN (Application-Layer Protocol Negotiation), which is used to select the HTTP/2 protocol during the TLS handshake.
*   **`client`**: Contains the client-side implementation of the CHTTP2 transport.
*   **`server`**: Contains the server-side implementation of the CHTTP2 transport.
*   **`transport`**: Contains the core implementation of the CHTTP2 transport, including the logic for parsing and serializing HTTP/2 frames, managing streams, and handling flow control.

## Files

*   **`transport/chttp2_transport.h`, `transport/chttp2_transport.cc`**: These files define the `Http2Transport` class, which is the main transport class.
*   **`transport/frame.h`, `transport/frame.cc`**: These files define the C++ classes that represent the different types of HTTP/2 frames.
*   **`transport/hpack_encoder.h`, `transport/hpack_encoder.cc`**: These files define the HPACK encoder.
*   **`transport/hpack_parser.h`, `transport/hpack_parser.cc`**: These files define the HPACK parser.
*   **`transport/flow_control.h`, `transport/flow_control.cc`**: These files define the flow control logic.
*   **`client/chttp2_connector.h`, `client/chttp2_connector.cc`**: These files define the client-side connector, which is responsible for creating a new CHTTP2 transport.
*   **`server/chttp2_server.h`, `server/chttp2_server.cc`**: These files define the server-side listener, which is responsible for accepting new connections and creating new CHTTP2 transports.

## Major Classes

*   **`grpc_core::chttp2::Http2Transport`**: The main transport class.
*   **`grpc_core::chttp2::Stream`**: Represents a single HTTP/2 stream.
*   **`grpc_core::chttp2::HpackEncoder`**: The HPACK encoder.
*   **`grpc_core::chttp2::HpackParser`**: The HPACK parser.

## Notes

*   The CHTTP2 transport is the default transport used by gRPC.
*   It is highly configurable and can be tuned for a variety of different workloads.
*   It supports all of the features of HTTP/2, including multiplexing, flow control, and header compression.
*   The CHTTP2 transport is a complex piece of machinery, and it can be difficult to understand how all of the different pieces fit together. The best way to understand it is to read the code and to trace the execution of a simple RPC.
