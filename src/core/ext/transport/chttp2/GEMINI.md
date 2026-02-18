# CHTTP2 and PH2 Transports

*   This directory contains the implementations of two HTTP/2 transports:
    *   The legacy CHTTP2 transport.
    *   The newer WIP promise-based HTTP/2 transport (PH2).

See also: [gRPC Transports overview](../../../transport/GEMINI.md)

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
*   Code in directories [`alpn`](./alpn), [`client`](./client) and [`server`](./server) is shared by PH2 and CHTTP2.

## 1. CHTTP2 (Legacy)

*   CHTTP2 is compatible with the Call V1 Stack.
*   Uses [`combiner`](../../../lib/iomgr/combiner.h) for concurrency.
*   CHTTP2 was the original default transport.
*   **Status:** Active and default, but planned for deprecation and removal after PH2 is fully rolled out and stable.

### CHTTP2 File Structure

*   General Transport Files:
    *   `chttp2_transport.{h,cc}`: Core transport logic for both client and server.
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

*   PH2 is compatible with the Call V3 Stack.
*   PH2 utilizes the gRPC promise framework (`src/core/lib/promise`) for asynchronous operations.
*   **Status:** Under Development.
*   **Rollout:** Expected to begin in July 2026.
*   **Experiments:**
    *   Client: `IsPromiseBasedHttp2ClientTransportEnabled()`.
    *   Server: `IsPromiseBasedHttp2ServerTransportEnabled()`.

### PH2 Goals

*   **Compatibility:** Compatibility with the new efficient Call V3 stack.
*   **Modernization:** Leverage the gRPC Promise API for cleaner and more maintainable asynchronous code.
*   **Performance:** Aim for equal or better performance compared to CHTTP2.
*   **Compliance:** Adhere to HTTP/2 RFC [RFC 9113](https://www.rfc-editor.org/rfc/rfc9113.html).
*   **Feature Parity:** Support all necessary features currently provided by CHTTP2.

### PH2 Development Guidelines

*   **Reference CHTTP2:** When implementing features in PH2, always first check the CHTTP2 implementation in this directory for reference. Use the mapping in the "Key classes in CHTTP2 and their PH2 equivalents" section below to find the PH2 counterparts.
*   **Asynchronous Operations:** All asynchronous operations *must* use the gRPC Promise library (`src/core/lib/promise`), particularly leveraging `Party` for concurrency. See the `Dependencies for PH2` section.
*   **Testing:** Any changes to PH2 code should be accompanied by relevant tests. Ensure that existing tests in `test/core/transport/chttp2/http2_client_transport_test.cc` and `test/core/transport/chttp2/http2_server_transport_test.cc` pass. Add new tests as needed to cover new functionality.
*   **Test Comments:** When writing new tests, include comments within each test explaining its purpose and assertions. For large tests, add comments for each step.

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
    *   `http2_transport.{h,cc}`: Common logic.
*   Frame Parsers and Validators for PH2:
    *   `frame.{h,cc}`: Newer frame parsing/serialization.
*   Assemblers:
    *   `header_assembler.h`: Converts gRPC Initial and Trailing Metadata into HTTP2 HEADER and CONTINUATION Frames and back.
    *   `message_assembler.h`: Converts gRPC Messages into HTTP2 DATA Frames and back.
*   Error Handling Classes:
    *   `http2_status.h`: Custom HTTP/2 error types (Stream vs Connection).
*   Ping and Keep Alive Helpers:
    *   `ping_promise.{h,cc}`
    *   `keepalive.{h,cc}`
*   Helper classes for PH2 writes:
    *   `stream_data_queue.h` Stores gRPC messages and Metadata from the CallV3 stack for each stream in a queue.
    *   `writable_streams.h` Track streams that have some data to send to the peer and have available flow control tokens.
*   Settings Helper : `http2_settings_promises.h`
*   Flow Control Helper : `flow_control_manager.h`
*   Stream : `stream.h` representation of each HTTP2 stream in the HTTP2 transport.
*   GoAway : `goaway.{h,cc}` for implementation of HTTP2 GOAWAY
*   Metadata: `incoming_metadata_tracker.h`
*   Security : `security_frame.h`

## 3. Common Files (Shared by CHTTP2 and PH2)

*   **`alpn`**: Contains code for ALPN (Application-Layer Protocol Negotiation), which is used to select the HTTP/2 protocol during the TLS handshake.
*   **`client/chttp2_connector.h`, `client/chttp2_connector.cc`**:
    *   These files define the client-side connector, which is responsible for creating a new HTTP2 transport.
    *   This connector creates either PH2 and CHTTP2 transport.
*   **`server/chttp2_server.h`, `server/chttp2_server.cc`**: These files define the server-side listener, which is responsible for accepting new connections and creating new HTTP2 transports.
*   Files in `transport/`:
    *   HPACK implementation:
        *   `hpack_constants.h`
        *   `hpack_encoder.{h,cc}`
        *   `hpack_encoder_table.{h,cc}`
        *   `hpack_parse_result.{h,cc}`
        *   `hpack_parser.{h,cc}`
        *   `hpack_parser_table.{h,cc}`
    *   Flow Control: `flow_control.{h,cc}`
    *   Settings: `http2_settings.{h,cc}`, `http2_settings_manager.{h,cc}`
    *   Ping policies: `ping_abuse_policy.{h,cc}`, `ping_callbacks.{h,cc}`, `ping_rate_policy.{h,cc}`
    *   Other utilities: `bin_encoder.{h,cc}`, `decode_huff.{h,cc}`, `huffsyms.{h,cc}`, `varint.{h,cc}`
    *   `transport_common.{h,cc}`
    *   `internal_channel_arg_names.h`
    *   `http2_ztrace_collector.h`: Collects events for z-trace debugging.
    *   `write_size_policy.{h,cc}`

## 4. Unused or TBD Files

*   Not used by either transport: `bin_decoder.{h,cc}`
*   TBD (ETA 2025-10-30):
    *   `call_tracer_wrapper.{h,cc}`
    *   `http2_stats_collector.{h,cc}`
    *   `http2_stats_collector.github.cc`

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
    *   `test/core/transport/chttp2/http2_client_transport_test.cc`: Main test suite for the PH2 client transport.
    *   `test/core/transport/chttp2/http2_server_transport_test.cc`: Main test suite for the PH2 server transport.
    *   `test/core/transport/chttp2/http2_transport_test.cc`: Common to PH2 Client and Server Transport. Tests code in `http2_transport.{h,cc}`.
    *   `test/core/transport/chttp2/frame_test.cc`
    *   `test/core/transport/chttp2/goaway_test.cc`
    *   `test/core/transport/chttp2/header_assembler_test.cc`
    *   `test/core/transport/chttp2/http2_status_test.cc`
    *   `test/core/transport/chttp2/keepalive_test.cc`
    *   `test/core/transport/chttp2/message_assembler_fuzz_test.cc`
    *   `test/core/transport/chttp2/message_assembler_test.cc`
    *   `test/core/transport/chttp2/ping_promise_test.cc`
    *   `test/core/transport/chttp2/settings_timeout_manager_test.cc`
    *   `test/core/transport/chttp2/settings_timeout_test.cc`
    *   `test/core/transport/chttp2/stream_data_queue_fuzz_test.cc`
    *   `test/core/transport/chttp2/stream_data_queue_test.cc`
    *   `test/core/transport/chttp2/writable_streams_fuzz_test.cc`
    *   `test/core/transport/chttp2/writable_streams_test.cc`
    *   `test/core/transport/chttp2/incoming_metadata_tracker_test.cc`
    *   `test/core/transport/chttp2/http2_security_frame_test.cc`

*   **Common Component Tests:**
    *   `test/core/transport/chttp2/flow_control_fuzzer.cc`
    *   `test/core/transport/chttp2/flow_control_manager_test.cc`
    *   `test/core/transport/chttp2/flow_control_test.cc`
    *   `test/core/transport/chttp2/hpack_encoder_test.cc`
    *   `test/core/transport/chttp2/hpack_parser_test.cc`
    *   `test/core/transport/chttp2/http2_settings_test.cc`
    *   `test/core/transport/chttp2/write_size_policy_fuzztest.cc`
    *   `test/core/transport/chttp2/write_size_policy_test.cc`

*   **PH2 End-to-end Tests:**
    *   `test/core/end2end/end2end_ph2_config.cc`: This file defines the test
    configuration for running end-to-end tests with PH2 transport.
    It enables/disables specific tests. It tests :
        1. PH2 client vs CHTTP2 server. (Done)
        1. CHTTP2 client vs PH2 server. (Coming Soon)
        1. PH2 client vs PH2 server. (Coming Soon)

## Dependencies for PH2

*   **gRPC Promise Library:**
    *   PH2 heavily relies on the gRPC Promise framework [`src/core/lib/promise/`](../../../lib/promise/GEMINI.md)
    *   Key components like [`party.h`](../../../lib/promise/party.h) are fundamental to PH2's async model.
*   **Call Spine:** PH2 interacts with the V3 Call Spine components located in [`src/core/call/`](../../../call/GEMINI.md).

## Similarities of PH2 and Chaotic Good

PH2 shares several architectural similarities with the [Chaotic Good transport](../chaotic_good/GEMINI.md) transport:

*   **Promise-Based:** Both transports are built upon the gRPC Promise library for managing asynchronous operations. This is a departure from the callback-based system in CHTTP2.
*   **Call V3 Stack:** Both are designed to work with the newer Call V3 stack.
*   **Client Transport Implementation:** The client transport implementations in PH2 (`http2_client_transport.h`) and Chaotic Good (`client_transport.h`) show conceptual similarities in how they handle stream creation, frame sending/receiving, and interaction with the promise-based event loop.
*   **Framing Concepts:** While the specific frame types and serialization differ (HTTP/2 vs. custom proto-based for Chaotic Good), the underlying concepts of defining frame structures (e.g., `frame.h` in both transports) and managing their serialization/deserialization are present in both.
*   **Endpoint Interaction:** Both transports use the `PromiseEndpoint` abstraction or reading from and writing to the network, making the core transport logic independent of the underlying I/O mechanism.
*   **Stream Initiation:** In PH2, `Http2ClientTransport::StartCall` initiates a new stream. It acquires a lock, assigns a new stream ID, creates a `Stream` object, and spawns the `CallOutboundLoop` to handle the stream's outgoing messages. Chaotic Good follows a similar pattern in `ChaoticGoodClientTransport::StartCall`, where it calls `StreamDispatch::MakeStream` to allocate a stream ID and create a `Stream` object, and then spawns `CallOutboundLoop` for the stream's lifecycle.

# PH2 Transport Party Slots

The HTTP2 transport uses Promise Party internally to manage scheduling of jobs.
Since the number of slots in a Party is 16, we need to account for all the slots
that we use in the transport.
We need to ensure that our slots do not exceed 16.

## PH2 Client Party Slots Usage

| Name | Category | Description | Max Spawns at a time | When is it spawned | Max Duration | Resolution |
|---|---|---|---|---|---|---|
| SecurityFrameLoop | Loop | Security Frame | 1 | After Constructor | Lifetime of the transport | Transport Close |
| ReadLoop | Loop | | 1 | After 1st write | Lifetime of the transport | Transport Close |
| FlowControlPeriodicUpdateLoop | Loop | | 1 | After Constructor | Lifetime of the transport | Transport Close |
| MultiplexerLoop | Loop | | 1 | After Constructor | Lifetime of the transport | Transport Close |
| AddData | Misc | ChannelZ AddData | 1 | On demand | Immediate | Immediate |
| CloseTransport | Misc | Close transport | 1 | While closing transport. Only once in the life of a transport | As long as it takes to close the transport | Transport Close |
| WaitForSettingsTimeout | Timeout | Settings Timeout | 1 | When we write SETTINGS | Settings timeout | Settings Ack Received or Settings Timeout |
| Keepalive | Loop | Keepalive Loop | 1 | If Keepalive is enabled, after constructor | Lifetime of the transport | Transport Close
| Ping | Timeout + Misc | | 4 | Sending a ping request | Timeout or a specific duration |
| | | **Total** | 12 | | | |

## PH2 Server Party Slots Usage

| Name | Category | Description | Max Spawns at a time | When is it spawned | Max Duration | Resolution |
|---|---|---|---|---|---|---|
