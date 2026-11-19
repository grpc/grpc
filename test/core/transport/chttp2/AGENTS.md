# CHTTP2 and PH2 Transport Tests

This folder contains tests for the CHTTP2 and PH2 transports.
For transport internals, see
[chttp2/AGENTS.md](../../../../src/core/ext/transport/chttp2/AGENTS.md).

## Keep This Document Current

**Keep this document in sync with the code it describes. This is mandatory.**

*   **Trigger:** Your CL adds, renames, moves, or deletes a file, BUILD target,
    class, function, experiment, or date that this document references.
*   **Action:** Search this document for the old name and fix every hit, in
    the same CL. Add new items to the relevant table or list. Remove deleted
    ones.
*   **Verify:** Before finishing, re-check every table, list, and count you
    touched against the source. Do not leave stale references behind.

## Must-Run PH2 Tests

Run these after any PH2 code change:

*   `http2_client_transport_test`
*   `http2_server_transport_test`

## PH2 Specific Tests

| File | BUILD Target | Rule | Code Under Test |
|---|---|---|---|
| `http2_client_transport_test.cc` | `http2_client_transport_test` | `grpc_cc_test` | `http2_client_transport.{h,cc}` |
| `http2_server_transport_test.cc` | `http2_server_transport_test` | `grpc_cc_test` | `http2_server_transport.{h,cc}` |
| `http2_transport_test.cc` | `http2_transport_test` | `grpc_cc_test` | `http2_transport.{h,cc}`, `http2_settings_promises.h`, `read_context.h`, `stream.h` |
| `frame_test.cc` | `frame_test` | `grpc_cc_test` | `frame.{h,cc}` |
| `goaway_test.cc` | `goaway_fuzz_test` | `grpc_fuzz_test` | `goaway.{h,cc}` |
| `header_assembler_test.cc` | `header_assembler_test` | `grpc_cc_test` | `header_assembler.h` |
| `http2_status_test.cc` | `http2_status_test` | `grpc_cc_test` | `http2_status.h` |
| `keepalive_test.cc` | `keepalive_test` | `grpc_fuzz_test` | `keepalive.{h,cc}` |
| `message_assembler_test.cc` | `message_assembler_test` | `grpc_cc_test` | `message_assembler.h` |
| `message_assembler_fuzz_test.cc` | `message_assembler_fuzz_test` | `grpc_fuzz_test` | `message_assembler.h` |
| `ping_promise_test.cc` | `ping_promise_test` | `grpc_fuzz_test` | `ping_promise.{h,cc}` |
| `settings_timeout_manager_test.cc` | `settings_timeout_manager_test` | `grpc_cc_test` | `http2_settings_promises.h` |
| `stream_data_queue_test.cc` | `stream_data_queue_test` | `grpc_cc_test` | `stream_data_queue.h` |
| `stream_data_queue_fuzz_test.cc` | `stream_data_queue_fuzz_test` | `grpc_fuzz_test` | `stream_data_queue.h` |
| `writable_streams_test.cc` | `writable_streams_test` | `grpc_cc_test` | `writable_streams.h` |
| `writable_streams_fuzz_test.cc` | `writable_streams_fuzz_test` | `grpc_fuzz_test` | `writable_streams.h` |
| `read_context_test.cc` | `read_context_test` | `grpc_cc_test` | `read_context.h` |
| `http2_security_frame_test.cc` | `http2_security_frame_test` | `grpc_cc_test` | `security_frame.h` |
| `write_cycle_test.cc` | `write_cycle_test` | `grpc_cc_test` | `write_cycle.{h,cc}` |
| `stream_test.cc` | `stream_test` | `grpc_cc_test` | `stream.h` |
| `flow_control_manager_test.cc` | `flow_control_manager_test` | `grpc_cc_test` | `flow_control_manager.h` |

Notes:

*   `http2_security_frame_test.cc` also has one CHTTP2 parser test.
*   Some targets are fuzz tests despite their `_test` names. See the Rule column.

### PH2 Test Helpers

*   `http2_common_test_inputs.h`: Shared constants, frame generators, gMock
    frame matchers, and the `Http2TransportTest` fixture.
*   `http2_frame_test_helper.h`: `Http2FrameTestHelper` builds serialized
    HTTP/2 frames for tests.

Shared promise-transport helpers in `test/core/transport/util/`:

*   `transport_test.h`: `TransportTest` is the base fixture of
    `Http2TransportTest`. `EventSequenceEndpoint` scripts endpoint reads and
    writes. Also used by Chaotic Good tests.
*   `mock_promise_endpoint.h`: gMock `MockEndpoint` and `MockPromiseEndpoint`.
    Used by `http2_transport_test.cc` and `read_context_test.cc`.

## Common Component Tests (Shared by CHTTP2 and PH2)

### HPACK Tests

| File | BUILD Target | Rule |
|---|---|---|
| `bin_encoder_test.cc` | `bin_encoder_test` | `grpc_cc_test` |
| `hpack_encoder_test.cc` | `hpack_encoder_test` | `grpc_cc_test` |
| `hpack_encoder_fuzzer.cc` | `hpack_encoder_fuzzer` | `grpc_fuzz_test` |
| `hpack_encoder_timeout_test.cc` | `hpack_encoder_timeout_test` | `grpc_fuzz_test` |
| `hpack_parser_test.cc` | `hpack_parser_test` | `grpc_cc_test` |
| `hpack_parser_fuzzer.cc` | `hpack_parser_fuzzer` | `grpc_fuzz_test` |
| `hpack_parser_input_size_fuzzer.cc` | `hpack_parser_input_size_fuzzer` | `grpc_fuzz_test` |
| `hpack_parser_table_test.cc` | `hpack_parser_table_test` | `grpc_cc_test` |
| `hpack_sync_fuzzer.cc` | `hpack_sync_fuzzer` | `grpc_fuzz_test` |
| `huff_fuzzer.cc` | `huff_fuzzer` | `grpc_fuzz_test` |
| `varint_test.cc` | `varint_test` | `grpc_cc_test` |

Notes:

*   Helper: `hpack_encoder_test_helper.h`.
*   `bin_encoder_test.cc` covers base64 and HPACK Huffman for binary metadata.
*   `huff_fuzzer.cc` covers HPACK Huffman encode and decode.
*   `varint_test.cc` covers HPACK integer encoding.

### Non-HPACK Tests

| File | BUILD Target | Rule |
|---|---|---|
| `alpn_test.cc` | `alpn_test` | `grpc_cc_test` |
| `chttp2_server_listener_test.cc` | `chttp2_server_listener_test` | `grpc_cc_test` |
| `flow_control_test.cc` | `flow_control_test` | `grpc_cc_test` |
| `flow_control_fuzzer.cc` | `flow_control_fuzzer` | `grpc_fuzz_test` |
| `http2_settings_test.cc` | `http2_settings_test` | `grpc_cc_test` |
| `ping_abuse_policy_test.cc` | `ping_abuse_policy_test` | `grpc_cc_test` |
| `ping_callbacks_test.cc` | `ping_callbacks_test` | `grpc_cc_test` |
| `ping_rate_policy_test.cc` | `ping_rate_policy_test` | `grpc_cc_test` |
| `write_size_policy_test.cc` | `write_size_policy_test` | `grpc_cc_test` |
| `write_size_policy_fuzztest.cc` | `write_size_policy_fuzztest` | `grpc_fuzz_test` |

## CHTTP2-Only Tests

| File | BUILD Target |
|---|---|
| `graceful_shutdown_test.cc` | `graceful_shutdown_test` |
| `ping_configuration_test.cc` | `ping_configuration_test` |
| `remove_stream_from_stalled_lists_test.cc` | `remove_stream_from_stalled_lists_test` |
| `settings_timeout_test.cc` | `settings_timeout_test` |
| `stream_leak_with_queued_flow_control_update_test.cc` | `stream_leak_with_queued_flow_control_update_test` |
| `streams_not_seen_test.cc` | `streams_not_seen_test` |
| `too_many_pings_test.cc` | `too_many_pings_test` |

## CHTTP2 End-to-End Tests

*   Suites are in `test/core/end2end/BUILD`.
*   No experiment gate. CHTTP2 is the default HTTP/2 transport.
*   Each suite expands to `<suite>_test`, `<suite>_fuzzer`,
    `<suite>_no_logging_test`, and `<suite>_no_logging_fuzzer`.

| Suite | Config | Coverage |
|---|---|---|
| `end2end_http2` | `end2end_http2_config.cc` | 9 configs: fullstack, proxy, socketpair |
| `end2end_http2_security` | `end2end_http2_security_config.cc` | 15 configs: fake, insecure, local TCP, SSL, TLS |
| `end2end_posix` | `end2end_posix_config.cc` | 9 configs: endpoint, fd, UDS, pipe wakeup |

Notes:

*   `end2end_posix` configs are `#ifdef`-gated. Count is for Linux.
*   Socketpair configs create the CHTTP2 transport directly. No client channel.
*   `end2end_vrpc` runs VRPC over CHTTP2 with 3 configs. Gated on
    `event_engine_client` and `event_engine_listener`.

## PH2 End-to-End Tests

*   Config: `test/core/end2end/end2end_ph2_config.cc`.
*   Suite `end2end_ph2` in `test/core/end2end/BUILD`.
*   Expands to `end2end_ph2_test`, `end2end_ph2_fuzzer`,
    `end2end_ph2_no_logging_test`, and `end2end_ph2_no_logging_fuzzer`.

| Configuration | Experiment Gate | Coverage |
|---|---|---|
| PH2 client vs CHTTP2 server | `ph2_client` | 17 configs |
| CHTTP2 client vs PH2 server | `ph2_server` | 1 insecure config |
| PH2 client vs PH2 server | `ph2_client_server` | 1 insecure config |

*   C++ end2end skips: see `test/cpp/end2end/AGENTS.md`.
