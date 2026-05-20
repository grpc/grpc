# Common Transport Utilities (lib/transport)

*   This directory contains common implementation details, abstractions, and utilities shared across various gRPC transport implementations (such as CHTTP2, PH2, Inproc, and Chaotic Good).

See also: [gRPC Transports overview](../../transport/GEMINI.md)

## Overarching Purpose

Files in this directory provide essential abstractions and reusable components that transport implementations depend on. Rather than each transport reinventing basic networking, state tracking, or encoding primitives, they utilize the APIs exposed here.

**Core Responsibilities:**
*   **Transport Abstraction Interfaces:** The core struct definitions for `grpc_transport` and `grpc_stream`.
*   **Promise-Based Networking:** Providing the `PromiseEndpoint` API to wrap traditional EventEngine endpoints with modern C++ Coroutine/Promise interfaces.
*   **Bandwidth Estimation:** Tracking bytes sent/received over time to dynamically compute Bandwidth-Delay Product (BDP) for flow control.
*   **State & Error Tracking:** Tracking connectivity states and converting error namespaces/types (e.g., HTTP/2 status/errors to `absl::Status` or gRPC status codes).
*   **Header Encoding & Parsing:** Encoding and decoding gRPC timeout headers securely and efficiently.

## Key Files and Components

*   **`transport.{h,cc}`**
    *   Defines the underlying `grpc_transport` and `grpc_stream` structs.
    *   Contains core serialization, statistic structures, and operational batching definitions (`grpc_transport_stream_op_batch`).
*   **`promise_endpoint.{h,cc}`**
    *   The `PromiseEndpoint` class provides a convenient API to run asynchronous read/write network streams using the gRPC Promise framework (`src/core/lib/promise`).
*   **`bdp_estimator.{h,cc}`**
    *   The `BdpEstimator` tracks traffic flow to adapt HTTP/2 window sizes (flow control) based on connection bandwidth and latency.
*   **`connectivity_state.{h,cc}`**
    *   Tracks `grpc_connectivity_state` transitions for channels/transports. Provides state change notifications.
*   **`timeout_encoding.{h,cc}`**
    *   Provides standard routines to encode and decode the `grpc-timeout` HTTP/2 header.
*   **`status_conversion.{h,cc}`**
    *   Utility functions converting between HTTP status codes, HTTP/2 errors (`grpc_chttp2_error_code`), and gRPC canonical status codes (`grpc_status_code`).
*   **`error_utils.{h,cc}`**
    *   Utilities for converting transit errors, raw `grpc_error_handle` objects, or `absl::Status` into status codes and error messages.
*   **`call_final_info.{h,cc}`**
    *   Struct definitions tracking transport-level metrics and statistics compiled when a call terminates.

## Dependencies and Guidelines

*   **Modern promise-based code:** New additions or modifications to `PromiseEndpoint` should strictly follow the conventions of the gRPC Promise framework located in `src/core/lib/promise/`.
*   **Compatibility:** Changes in this directory affect all transport types. Extra caution should be taken to avoid breaking behavior across CHTTP2, PH2, and chaotic_good transports. Ensure their respective unit and end-to-end tests run after making any modifications here.
