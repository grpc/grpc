# CHTTP2 and PH2 Transports

*   This directory contains the implementations of two HTTP/2 transports:
    *   The legacy CHTTP2 transport.
    *   The newer WIP promise-based HTTP/2 transport (PH2).

See also: [gRPC Transports overview](../GEMINI.md)

## Overarching Purpose

This directory houses the C++ implementations of gRPC's HTTP/2 transports.
These transports manage the low-level details of the HTTP/2 protocol,
serving as the bridge between gRPC's high-level abstractions (channels, calls)
and the underlying endpoint.

**Core Responsibilities:**

*   **HTTP/2 Framing:** Parsing and serializing HTTP/2 frames (HEADERS, DATA, SETTINGS, PING, GOAWAY, WINDOW_UPDATE, RST_STREAM, CONTINUATION).
*   **Stream Multiplexing:** Managing multiple concurrent gRPC calls over a single TCP connection using HTTP/2 streams.
*   **Flow Control:** Implementing HTTP/2 flow control mechanisms (both stream and connection level) to prevent overload.
*   **Header Compression:** Utilizing HPACK to compress and decompress header fields, reducing bandwidth usage.
*   **Error Handling:** Detecting and reporting HTTP/2 protocol errors and stream errors.

**Implementented RFCs:**

*   [RFC 9113: HTTP/2](https://www.rfc-editor.org/rfc/rfc9113.html)
*   [RFC 7541: HPACK - Header Compression for HTTP/2](https://www.rfc-editor.org/rfc/rfc7541.html)

## Subdirectories

*   **`alpn`**: Contains code for ALPN (Application-Layer Protocol Negotiation), which is used to select the HTTP/2 protocol during the TLS handshake.
*   **`client`**: Contains the client-side implementation of the CHTTP2 transport.
*   **`server`**: Contains the server-side implementation of the CHTTP2 transport.
*   **`transport`**: Contains the core implementation of the CHTTP2 and PH2 transport.

## 1. CHTTP2 (Legacy)

*   This is compatible with the Call V1 Stack.
*   Uses [`combiner`](../../lib/iomgr/combiner.h) for concurrency.
*   This was the original default transport.
*   **Status:** Active and default, but planned for deprecation and removal after PH2 is fully rolled out and stable.

### CHTTP2 File Structure

*   General Transport Files:
    *   `chttp2_transport.{h,cc}`: Core transport logic.
    *   `internal.h`: Internal declarations for CHTTP2.
    *   `parsing.cc`: HTTP/2 incoming frame parsing and processing.
    *   `stream_lists.{h,cc}`: Manages stream lists.
    *   `writing.cc`: Handles writing data to the wire.
*   Frame Specific Files: `frame_*.h`, `frame_*.cc`
    *   `frame_data.{h,cc}`
    *   `frame_goaway.{h,cc}`
    *   `frame_ping.{h,cc}`
    *   `frame_rst_stream.{h,cc}`
    *   `frame_security.{h,cc}`
    *   `frame_settings.{h,cc}`
    *   `frame_window_update.{h,cc}`
    *   `legacy_frame.h`

## 2. PH2 (Promise-Based HTTP/2)

*   This is compatible with the Call V3 Stack.
*   Utilizes the gRPC promise framework (`src/core/lib/promise`) for asynchronous operations.
*   **Status:** Under Development.
*   **Rollout:** Expected to begin in January 2026.

### PH2 Goals

*   **Compatibility:** Compatibility with the new efficient Call V3 stack.
*   **Modernization:** Leverage the gRPC Promise API for cleaner and more maintainable asynchronous code.
*   **Performance:** Aim for equal or better performance compared to CHTTP2.
*   **Compliance:** Stricter adherence to HTTP/2 RFC [RFC 9113](https://www.rfc-editor.org/rfc/rfc9113.html).
*   **Feature Parity:** Support all necessary features currently provided by CHTTP2.

### Key Differences from CHTTP2

*   **Concurrency Model:** Uses `Party` promises instead of `combiner`.
*   **Frame Handling:** New frame parsing and serialization logic in `frame.{h,cc}`.
*   **Stream Management:** Different data structures and mechanisms.

### PH2 File Structure

*   PH2 Client Code:
    *   `http2_client_transport.{h,cc}`
*   PH2 Server Code:
    *   `http2_server_transport.{h,cc}`
*   Code common to PH2 Client and PH2 Server:
    *   `http2_transport.{h,cc}`: Base class and common logic.
*   Frame Parsers and Validators for PH2:
    *   `frame.{h,cc}`: Newer frame parsing/serialization.
*   Assemblers:
    *   `header_assembler.{h,cc}`: Converts gRPC Initial and Trailing Metadata into HTTP2 HEADER and CONTINUATION Frames and back.
    *   `message_assembler.{h,cc}`: Converts gRPC Messages into HTTP2 DATA Frames and back.
*   Error Handling Classes:
    *   `http2_status.h`: Custom HTTP/2 error types (Stream vs Connection).
*   Ping and Keep Alive Helpers:
    *   `ping_promise.{h,cc}`
    *   `keepalive.{h,cc}`
*   Helper classes for PH2 writes:
    *   `stream_data_queue.{h,cc}` Stores gRPC messages and Metadata from the CallV3 stack for each stream in a queue.
    *   `writable_streams.{h,cc}` Track streams that have some data to send to the peer and have available flow control tokens.
*   Settings Helper : `http2_settings_promises.h`
*   Flow Control Helper : `flow_control_manager.h`

## 3. Common Files (Shared by CHTTP2 and PH2)

*   **`alpn`**: Contains code for ALPN (Application-Layer Protocol Negotiation), which is used to select the HTTP/2 protocol during the TLS handshake.
*   **`client/chttp2_connector.h`, `client/chttp2_connector.cc`**: These files define the client-side connector, which is responsible for creating a new CHTTP2 transport.
*   **`server/chttp2_server.h`, `server/chttp2_server.cc`**: These files define the server-side listener, which is responsible for accepting new connections and creating new CHTTP2 transports.
*   HPACK implementation: `hpack_*.{h,cc}` (e.g., `hpack_encoder.cc`, `hpack_parser.cc`)
*   Flow Control: `flow_control.{h,cc}`
*   Settings: `http2_settings*.{h,cc}` (e.g., `http2_settings.cc`, `http2_settings_manager.cc`)
*   Ping policies: `ping_abuse_policy.{h,cc}`, `ping_callbacks.{h,cc}`, `ping_rate_policy.{h,cc}`
*   Other utilities: `bin_encoder.{h,cc}`, `decode_huff.{h,cc}`, `huffsyms.{h,cc}`, `varint.{h,cc}`
*   `transport_common.{h,cc}`
*   `internal_channel_arg_names.h`

## 4. Unused or TBD Files

*   Not used by either transport: `bin_decoder.{h,cc}`
*   TBD (ETA 2025-10-30):
    *   `call_tracer_wrapper.{h,cc}`
    *   `http2_stats_collector.{h,cc}`
    *   `http2_stats_collector.github.cc`
    *   `http2_ztrace_collector.h`
    *   `write_size_policy.{h,cc}`

## Key classes in CHTTP2 and their PH2 equivalents

This section maps the core functionalities and classes from the legacy CHTTP2
transport to their counterparts in the newer Promise-based PH2 transport.

*   **Transport Object**:
    *   CHTTP2: `grpc_chttp2_transport` struct defined in `chttp2_transport.cc`.
    *   PH2: `Http2ClientTransport` class in `http2_client_transport.h` for clients, and `Http2ServerTransport` class in `http2_server_transport.h` for servers.

*   **Stream Object**:
    *   CHTTP2: `grpc_chttp2_stream` struct defined in `chttp2_transport.cc`.
    *   PH2: `Http2ClientTransport::Stream` inner class for clients, and `Http2ServerTransport::Stream` inner class for servers.

*   **Transport Creation**:
    *   CHTTP2: `grpc_create_chttp2_transport()` in `chttp2_transport.cc`.
    *   PH2: Constructors of `Http2ClientTransport` and `Http2ServerTransport`.

*   **Stream Initiation/Handling**:
    *   CHTTP2: Functions like `init_stream`, `chttp2_perform_stream_op_locked`, etc., in `chttp2_transport.cc`.
    *   PH2: Handled within `Http2ClientTransport::StartCall` for clients, and `Http2ServerTransport::SetCallDestination` for servers.

*   **Error Handling**:
    *   CHTTP2: Error handling with `grpc_error_handle` throughout the code.
    *   PH2: `http2_status.h` for custom error types, with `HandleError` methods in the transport classes to process stream/connection errors.

## Test Files

Key test files include:

*   **PH2 Specific Tests:**
    *   `test/core/transport/chttp2/http2_client_transport_test.cc`
    *   `test/core/transport/chttp2/frame_test.cc`
    *   `test/core/transport/chttp2/ping_promise_test.cc`
    *   `test/core/transport/chttp2/stream_data_queue_test.cc`
    *   `test/core/transport/chttp2/header_assembler_test.cc`
    *   `test/core/transport/chttp2/message_assembler_test.cc`
    *   `test/core/transport/chttp2/keepalive_test.cc`
    *   `test/core/transport/chttp2/writable_streams_test.cc`

*   **Common Component Tests:**
    *   `test/core/transport/chttp2/http2_settings_test.cc`
    *   `test/core/transport/chttp2/hpack_encoder_test.cc`
    *   `test/core/transport/chttp2/hpack_parser_test.cc`
    *   `test/core/transport/chttp2/flow_control_test.cc`

## Dependencies for PH2

*   **gRPC Promise Library:**
    *   PH2 heavily relies on the gRPC Promise framework [`src/core/lib/promise/`](../lib/promise/GEMINI.md)
    *   Key components like [`party.h`](../lib/promise/party.h) are fundamental to PH2's async model.
*   **Call Spine:** PH2 interacts with the V3 Call Spine components located in [`src/core/call/`](../../call/GEMINI.md).
