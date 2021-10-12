
/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* This file is auto-generated */

#include "test/core/end2end/end2end_tests.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/log.h>


static bool g_pre_init_called = false;

extern void authority_not_supported(grpc_end2end_test_config config);
extern void authority_not_supported_pre_init(void);
extern void bad_hostname(grpc_end2end_test_config config);
extern void bad_hostname_pre_init(void);
extern void bad_ping(grpc_end2end_test_config config);
extern void bad_ping_pre_init(void);
extern void binary_metadata(grpc_end2end_test_config config);
extern void binary_metadata_pre_init(void);
extern void call_creds(grpc_end2end_test_config config);
extern void call_creds_pre_init(void);
extern void call_host_override(grpc_end2end_test_config config);
extern void call_host_override_pre_init(void);
extern void cancel_after_accept(grpc_end2end_test_config config);
extern void cancel_after_accept_pre_init(void);
extern void cancel_after_client_done(grpc_end2end_test_config config);
extern void cancel_after_client_done_pre_init(void);
extern void cancel_after_invoke(grpc_end2end_test_config config);
extern void cancel_after_invoke_pre_init(void);
extern void cancel_after_round_trip(grpc_end2end_test_config config);
extern void cancel_after_round_trip_pre_init(void);
extern void cancel_before_invoke(grpc_end2end_test_config config);
extern void cancel_before_invoke_pre_init(void);
extern void cancel_in_a_vacuum(grpc_end2end_test_config config);
extern void cancel_in_a_vacuum_pre_init(void);
extern void cancel_with_status(grpc_end2end_test_config config);
extern void cancel_with_status_pre_init(void);
extern void channelz(grpc_end2end_test_config config);
extern void channelz_pre_init(void);
extern void client_streaming(grpc_end2end_test_config config);
extern void client_streaming_pre_init(void);
extern void compressed_payload(grpc_end2end_test_config config);
extern void compressed_payload_pre_init(void);
extern void connectivity(grpc_end2end_test_config config);
extern void connectivity_pre_init(void);
extern void default_host(grpc_end2end_test_config config);
extern void default_host_pre_init(void);
extern void disappearing_server(grpc_end2end_test_config config);
extern void disappearing_server_pre_init(void);
extern void empty_batch(grpc_end2end_test_config config);
extern void empty_batch_pre_init(void);
extern void filter_causes_close(grpc_end2end_test_config config);
extern void filter_causes_close_pre_init(void);
extern void filter_context(grpc_end2end_test_config config);
extern void filter_context_pre_init(void);
extern void filter_init_fails(grpc_end2end_test_config config);
extern void filter_init_fails_pre_init(void);
extern void filter_latency(grpc_end2end_test_config config);
extern void filter_latency_pre_init(void);
extern void filter_status_code(grpc_end2end_test_config config);
extern void filter_status_code_pre_init(void);
extern void graceful_server_shutdown(grpc_end2end_test_config config);
extern void graceful_server_shutdown_pre_init(void);
extern void high_initial_seqno(grpc_end2end_test_config config);
extern void high_initial_seqno_pre_init(void);
extern void hpack_size(grpc_end2end_test_config config);
extern void hpack_size_pre_init(void);
extern void idempotent_request(grpc_end2end_test_config config);
extern void idempotent_request_pre_init(void);
extern void invoke_large_request(grpc_end2end_test_config config);
extern void invoke_large_request_pre_init(void);
extern void keepalive_timeout(grpc_end2end_test_config config);
extern void keepalive_timeout_pre_init(void);
extern void large_metadata(grpc_end2end_test_config config);
extern void large_metadata_pre_init(void);
extern void max_concurrent_streams(grpc_end2end_test_config config);
extern void max_concurrent_streams_pre_init(void);
extern void max_connection_age(grpc_end2end_test_config config);
extern void max_connection_age_pre_init(void);
extern void max_connection_idle(grpc_end2end_test_config config);
extern void max_connection_idle_pre_init(void);
extern void max_message_length(grpc_end2end_test_config config);
extern void max_message_length_pre_init(void);
extern void negative_deadline(grpc_end2end_test_config config);
extern void negative_deadline_pre_init(void);
extern void no_error_on_hotpath(grpc_end2end_test_config config);
extern void no_error_on_hotpath_pre_init(void);
extern void no_logging(grpc_end2end_test_config config);
extern void no_logging_pre_init(void);
extern void no_op(grpc_end2end_test_config config);
extern void no_op_pre_init(void);
extern void payload(grpc_end2end_test_config config);
extern void payload_pre_init(void);
extern void ping(grpc_end2end_test_config config);
extern void ping_pre_init(void);
extern void ping_pong_streaming(grpc_end2end_test_config config);
extern void ping_pong_streaming_pre_init(void);
extern void proxy_auth(grpc_end2end_test_config config);
extern void proxy_auth_pre_init(void);
extern void registered_call(grpc_end2end_test_config config);
extern void registered_call_pre_init(void);
extern void request_with_flags(grpc_end2end_test_config config);
extern void request_with_flags_pre_init(void);
extern void request_with_payload(grpc_end2end_test_config config);
extern void request_with_payload_pre_init(void);
extern void resource_quota_server(grpc_end2end_test_config config);
extern void resource_quota_server_pre_init(void);
extern void retry(grpc_end2end_test_config config);
extern void retry_pre_init(void);
extern void retry_cancel_during_delay(grpc_end2end_test_config config);
extern void retry_cancel_during_delay_pre_init(void);
extern void retry_cancel_with_multiple_send_batches(grpc_end2end_test_config config);
extern void retry_cancel_with_multiple_send_batches_pre_init(void);
extern void retry_cancellation(grpc_end2end_test_config config);
extern void retry_cancellation_pre_init(void);
extern void retry_disabled(grpc_end2end_test_config config);
extern void retry_disabled_pre_init(void);
extern void retry_exceeds_buffer_size_in_delay(grpc_end2end_test_config config);
extern void retry_exceeds_buffer_size_in_delay_pre_init(void);
extern void retry_exceeds_buffer_size_in_initial_batch(grpc_end2end_test_config config);
extern void retry_exceeds_buffer_size_in_initial_batch_pre_init(void);
extern void retry_exceeds_buffer_size_in_subsequent_batch(grpc_end2end_test_config config);
extern void retry_exceeds_buffer_size_in_subsequent_batch_pre_init(void);
extern void retry_lb_drop(grpc_end2end_test_config config);
extern void retry_lb_drop_pre_init(void);
extern void retry_lb_fail(grpc_end2end_test_config config);
extern void retry_lb_fail_pre_init(void);
extern void retry_non_retriable_status(grpc_end2end_test_config config);
extern void retry_non_retriable_status_pre_init(void);
extern void retry_non_retriable_status_before_recv_trailing_metadata_started(grpc_end2end_test_config config);
extern void retry_non_retriable_status_before_recv_trailing_metadata_started_pre_init(void);
extern void retry_per_attempt_recv_timeout(grpc_end2end_test_config config);
extern void retry_per_attempt_recv_timeout_pre_init(void);
extern void retry_per_attempt_recv_timeout_on_last_attempt(grpc_end2end_test_config config);
extern void retry_per_attempt_recv_timeout_on_last_attempt_pre_init(void);
extern void retry_recv_initial_metadata(grpc_end2end_test_config config);
extern void retry_recv_initial_metadata_pre_init(void);
extern void retry_recv_message(grpc_end2end_test_config config);
extern void retry_recv_message_pre_init(void);
extern void retry_recv_trailing_metadata_error(grpc_end2end_test_config config);
extern void retry_recv_trailing_metadata_error_pre_init(void);
extern void retry_send_initial_metadata_refs(grpc_end2end_test_config config);
extern void retry_send_initial_metadata_refs_pre_init(void);
extern void retry_send_op_fails(grpc_end2end_test_config config);
extern void retry_send_op_fails_pre_init(void);
extern void retry_server_pushback_delay(grpc_end2end_test_config config);
extern void retry_server_pushback_delay_pre_init(void);
extern void retry_server_pushback_disabled(grpc_end2end_test_config config);
extern void retry_server_pushback_disabled_pre_init(void);
extern void retry_streaming(grpc_end2end_test_config config);
extern void retry_streaming_pre_init(void);
extern void retry_streaming_after_commit(grpc_end2end_test_config config);
extern void retry_streaming_after_commit_pre_init(void);
extern void retry_streaming_succeeds_before_replay_finished(grpc_end2end_test_config config);
extern void retry_streaming_succeeds_before_replay_finished_pre_init(void);
extern void retry_throttled(grpc_end2end_test_config config);
extern void retry_throttled_pre_init(void);
extern void retry_too_many_attempts(grpc_end2end_test_config config);
extern void retry_too_many_attempts_pre_init(void);
extern void sdk_authz(grpc_end2end_test_config config);
extern void sdk_authz_pre_init(void);
extern void server_finishes_request(grpc_end2end_test_config config);
extern void server_finishes_request_pre_init(void);
extern void server_streaming(grpc_end2end_test_config config);
extern void server_streaming_pre_init(void);
extern void shutdown_finishes_calls(grpc_end2end_test_config config);
extern void shutdown_finishes_calls_pre_init(void);
extern void shutdown_finishes_tags(grpc_end2end_test_config config);
extern void shutdown_finishes_tags_pre_init(void);
extern void simple_cacheable_request(grpc_end2end_test_config config);
extern void simple_cacheable_request_pre_init(void);
extern void simple_delayed_request(grpc_end2end_test_config config);
extern void simple_delayed_request_pre_init(void);
extern void simple_metadata(grpc_end2end_test_config config);
extern void simple_metadata_pre_init(void);
extern void simple_request(grpc_end2end_test_config config);
extern void simple_request_pre_init(void);
extern void stream_compression_compressed_payload(grpc_end2end_test_config config);
extern void stream_compression_compressed_payload_pre_init(void);
extern void stream_compression_payload(grpc_end2end_test_config config);
extern void stream_compression_payload_pre_init(void);
extern void stream_compression_ping_pong_streaming(grpc_end2end_test_config config);
extern void stream_compression_ping_pong_streaming_pre_init(void);
extern void streaming_error_response(grpc_end2end_test_config config);
extern void streaming_error_response_pre_init(void);
extern void trailing_metadata(grpc_end2end_test_config config);
extern void trailing_metadata_pre_init(void);
extern void write_buffering(grpc_end2end_test_config config);
extern void write_buffering_pre_init(void);
extern void write_buffering_at_end(grpc_end2end_test_config config);
extern void write_buffering_at_end_pre_init(void);


static grpc_end2end_test_case_config configs[] = {
  {
    "authority_not_supported",
    authority_not_supported_pre_init,
    authority_not_supported,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "bad_hostname",
    bad_hostname_pre_init,
    bad_hostname,
    /* grpc_end2end_test_case_options */
    {false, false, true, true, false, false, false, false, false, false, false},
  },
  {
    "bad_ping",
    bad_ping_pre_init,
    bad_ping,
    /* grpc_end2end_test_case_options */
    {true, false, false, false, false, false, false, false, false, false, false},
  },
  {
    "binary_metadata",
    binary_metadata_pre_init,
    binary_metadata,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "call_creds",
    call_creds_pre_init,
    call_creds,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, true, false, false, false, false, false, false},
  },
  {
    "call_host_override",
    call_host_override_pre_init,
    call_host_override,
    /* grpc_end2end_test_case_options */
    {true, true, true, true, false, false, false, false, false, false, false},
  },
  {
    "cancel_after_accept",
    cancel_after_accept_pre_init,
    cancel_after_accept,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "cancel_after_client_done",
    cancel_after_client_done_pre_init,
    cancel_after_client_done,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "cancel_after_invoke",
    cancel_after_invoke_pre_init,
    cancel_after_invoke,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "cancel_after_round_trip",
    cancel_after_round_trip_pre_init,
    cancel_after_round_trip,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "cancel_before_invoke",
    cancel_before_invoke_pre_init,
    cancel_before_invoke,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "cancel_in_a_vacuum",
    cancel_in_a_vacuum_pre_init,
    cancel_in_a_vacuum,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "cancel_with_status",
    cancel_with_status_pre_init,
    cancel_with_status,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "channelz",
    channelz_pre_init,
    channelz,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "client_streaming",
    client_streaming_pre_init,
    client_streaming,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "compressed_payload",
    compressed_payload_pre_init,
    compressed_payload,
    /* grpc_end2end_test_case_options */
    {false, false, false, false, false, false, true, false, false, false, false},
  },
  {
    "connectivity",
    connectivity_pre_init,
    connectivity,
    /* grpc_end2end_test_case_options */
    {true, false, true, false, false, false, false, false, false, false, false},
  },
  {
    "default_host",
    default_host_pre_init,
    default_host,
    /* grpc_end2end_test_case_options */
    {true, true, true, true, false, false, false, false, false, false, false},
  },
  {
    "disappearing_server",
    disappearing_server_pre_init,
    disappearing_server,
    /* grpc_end2end_test_case_options */
    {true, false, true, true, false, false, false, false, false, false, false},
  },
  {
    "empty_batch",
    empty_batch_pre_init,
    empty_batch,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "filter_causes_close",
    filter_causes_close_pre_init,
    filter_causes_close,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "filter_context",
    filter_context_pre_init,
    filter_context,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "filter_init_fails",
    filter_init_fails_pre_init,
    filter_init_fails,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "filter_latency",
    filter_latency_pre_init,
    filter_latency,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "filter_status_code",
    filter_status_code_pre_init,
    filter_status_code,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "graceful_server_shutdown",
    graceful_server_shutdown_pre_init,
    graceful_server_shutdown,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, true, false, false, false, false},
  },
  {
    "high_initial_seqno",
    high_initial_seqno_pre_init,
    high_initial_seqno,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "hpack_size",
    hpack_size_pre_init,
    hpack_size,
    /* grpc_end2end_test_case_options */
    {false, false, false, false, false, false, true, false, false, false, false},
  },
  {
    "idempotent_request",
    idempotent_request_pre_init,
    idempotent_request,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "invoke_large_request",
    invoke_large_request_pre_init,
    invoke_large_request,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "keepalive_timeout",
    keepalive_timeout_pre_init,
    keepalive_timeout,
    /* grpc_end2end_test_case_options */
    {false, false, false, false, false, false, false, true, false, false, false},
  },
  {
    "large_metadata",
    large_metadata_pre_init,
    large_metadata,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "max_concurrent_streams",
    max_concurrent_streams_pre_init,
    max_concurrent_streams,
    /* grpc_end2end_test_case_options */
    {false, false, false, false, false, false, true, false, false, false, false},
  },
  {
    "max_connection_age",
    max_connection_age_pre_init,
    max_connection_age,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, true, false, false, false, false},
  },
  {
    "max_connection_idle",
    max_connection_idle_pre_init,
    max_connection_idle,
    /* grpc_end2end_test_case_options */
    {true, false, false, false, false, false, false, false, false, false, false},
  },
  {
    "max_message_length",
    max_message_length_pre_init,
    max_message_length,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "negative_deadline",
    negative_deadline_pre_init,
    negative_deadline,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "no_error_on_hotpath",
    no_error_on_hotpath_pre_init,
    no_error_on_hotpath,
    /* grpc_end2end_test_case_options */
    {false, false, false, false, false, false, false, false, false, false, false},
  },
  {
    "no_logging",
    no_logging_pre_init,
    no_logging,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "no_op",
    no_op_pre_init,
    no_op,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "payload",
    payload_pre_init,
    payload,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "ping",
    ping_pre_init,
    ping,
    /* grpc_end2end_test_case_options */
    {true, false, false, false, false, false, false, false, false, false, false},
  },
  {
    "ping_pong_streaming",
    ping_pong_streaming_pre_init,
    ping_pong_streaming,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "proxy_auth",
    proxy_auth_pre_init,
    proxy_auth,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, true, false, false},
  },
  {
    "registered_call",
    registered_call_pre_init,
    registered_call,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "request_with_flags",
    request_with_flags_pre_init,
    request_with_flags,
    /* grpc_end2end_test_case_options */
    {false, false, false, false, false, false, false, false, false, false, false},
  },
  {
    "request_with_payload",
    request_with_payload_pre_init,
    request_with_payload,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "resource_quota_server",
    resource_quota_server_pre_init,
    resource_quota_server,
    /* grpc_end2end_test_case_options */
    {false, false, false, false, false, false, false, false, false, false, false},
  },
  {
    "retry",
    retry_pre_init,
    retry,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_cancel_during_delay",
    retry_cancel_during_delay_pre_init,
    retry_cancel_during_delay,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_cancel_with_multiple_send_batches",
    retry_cancel_with_multiple_send_batches_pre_init,
    retry_cancel_with_multiple_send_batches,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_cancellation",
    retry_cancellation_pre_init,
    retry_cancellation,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_disabled",
    retry_disabled_pre_init,
    retry_disabled,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_exceeds_buffer_size_in_delay",
    retry_exceeds_buffer_size_in_delay_pre_init,
    retry_exceeds_buffer_size_in_delay,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_exceeds_buffer_size_in_initial_batch",
    retry_exceeds_buffer_size_in_initial_batch_pre_init,
    retry_exceeds_buffer_size_in_initial_batch,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_exceeds_buffer_size_in_subsequent_batch",
    retry_exceeds_buffer_size_in_subsequent_batch_pre_init,
    retry_exceeds_buffer_size_in_subsequent_batch,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_lb_drop",
    retry_lb_drop_pre_init,
    retry_lb_drop,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_lb_fail",
    retry_lb_fail_pre_init,
    retry_lb_fail,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_non_retriable_status",
    retry_non_retriable_status_pre_init,
    retry_non_retriable_status,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_non_retriable_status_before_recv_trailing_metadata_started",
    retry_non_retriable_status_before_recv_trailing_metadata_started_pre_init,
    retry_non_retriable_status_before_recv_trailing_metadata_started,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_per_attempt_recv_timeout",
    retry_per_attempt_recv_timeout_pre_init,
    retry_per_attempt_recv_timeout,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_per_attempt_recv_timeout_on_last_attempt",
    retry_per_attempt_recv_timeout_on_last_attempt_pre_init,
    retry_per_attempt_recv_timeout_on_last_attempt,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_recv_initial_metadata",
    retry_recv_initial_metadata_pre_init,
    retry_recv_initial_metadata,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_recv_message",
    retry_recv_message_pre_init,
    retry_recv_message,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_recv_trailing_metadata_error",
    retry_recv_trailing_metadata_error_pre_init,
    retry_recv_trailing_metadata_error,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_send_initial_metadata_refs",
    retry_send_initial_metadata_refs_pre_init,
    retry_send_initial_metadata_refs,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_send_op_fails",
    retry_send_op_fails_pre_init,
    retry_send_op_fails,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_server_pushback_delay",
    retry_server_pushback_delay_pre_init,
    retry_server_pushback_delay,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_server_pushback_disabled",
    retry_server_pushback_disabled_pre_init,
    retry_server_pushback_disabled,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_streaming",
    retry_streaming_pre_init,
    retry_streaming,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_streaming_after_commit",
    retry_streaming_after_commit_pre_init,
    retry_streaming_after_commit,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_streaming_succeeds_before_replay_finished",
    retry_streaming_succeeds_before_replay_finished_pre_init,
    retry_streaming_succeeds_before_replay_finished,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_throttled",
    retry_throttled_pre_init,
    retry_throttled,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "retry_too_many_attempts",
    retry_too_many_attempts_pre_init,
    retry_too_many_attempts,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, true},
  },
  {
    "sdk_authz",
    sdk_authz_pre_init,
    sdk_authz,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, true, false, false, false, false, false, false},
  },
  {
    "server_finishes_request",
    server_finishes_request_pre_init,
    server_finishes_request,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "server_streaming",
    server_streaming_pre_init,
    server_streaming,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, true, false, false, false},
  },
  {
    "shutdown_finishes_calls",
    shutdown_finishes_calls_pre_init,
    shutdown_finishes_calls,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "shutdown_finishes_tags",
    shutdown_finishes_tags_pre_init,
    shutdown_finishes_tags,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "simple_cacheable_request",
    simple_cacheable_request_pre_init,
    simple_cacheable_request,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "simple_delayed_request",
    simple_delayed_request_pre_init,
    simple_delayed_request,
    /* grpc_end2end_test_case_options */
    {true, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "simple_metadata",
    simple_metadata_pre_init,
    simple_metadata,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "simple_request",
    simple_request_pre_init,
    simple_request,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "stream_compression_compressed_payload",
    stream_compression_compressed_payload_pre_init,
    stream_compression_compressed_payload,
    /* grpc_end2end_test_case_options */
    {false, false, false, false, false, false, true, false, false, false, false},
  },
  {
    "stream_compression_payload",
    stream_compression_payload_pre_init,
    stream_compression_payload,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, true, false, false, false, false},
  },
  {
    "stream_compression_ping_pong_streaming",
    stream_compression_ping_pong_streaming_pre_init,
    stream_compression_ping_pong_streaming,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, true, false, false, false, false},
  },
  {
    "streaming_error_response",
    streaming_error_response_pre_init,
    streaming_error_response,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "trailing_metadata",
    trailing_metadata_pre_init,
    trailing_metadata,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, false, false},
  },
  {
    "write_buffering",
    write_buffering_pre_init,
    write_buffering,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, true, false},
  },
  {
    "write_buffering_at_end",
    write_buffering_at_end_pre_init,
    write_buffering_at_end,
    /* grpc_end2end_test_case_options */
    {false, false, false, true, false, false, false, false, false, true, false},
  },
};

static grpc_end2end_test_fixture_config fixture_configs[] = {
  {
    "h2_census",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_compress",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_fakesec",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_fd",
    /* grpc_end2end_test_fixture_options */
    {false, false, false, true, true, false, false, true, false, true, false},
  },
  {
    "h2_full",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_full+pipe",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_full+trace",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, true, false, true, false, true, true},
  },
  {
    "h2_http_proxy",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, false, false, true, true, true, true},
  },
  {
    "h2_insecure",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_local_ipv4",
    /* grpc_end2end_test_fixture_options */
    {true, false, false, true, true, false, false, true, false, true, true},
  },
  {
    "h2_local_ipv6",
    /* grpc_end2end_test_fixture_options */
    {true, false, false, true, true, false, false, true, false, true, true},
  },
  {
    "h2_local_uds",
    /* grpc_end2end_test_fixture_options */
    {true, false, false, true, true, false, false, true, false, true, true},
  },
  {
    "h2_oauth2",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_proxy",
    /* grpc_end2end_test_fixture_options */
    {true, true, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_sockpair",
    /* grpc_end2end_test_fixture_options */
    {false, false, false, true, true, false, false, true, false, true, false},
  },
  {
    "h2_sockpair+trace",
    /* grpc_end2end_test_fixture_options */
    {false, false, false, true, true, true, false, true, false, true, false},
  },
  {
    "h2_sockpair_1byte",
    /* grpc_end2end_test_fixture_options */
    {false, false, false, true, true, false, false, true, false, true, false},
  },
  {
    "h2_ssl",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_ssl_cred_reload",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_ssl_proxy",
    /* grpc_end2end_test_fixture_options */
    {true, true, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_tls",
    /* grpc_end2end_test_fixture_options */
    {true, false, true, true, true, false, false, true, false, true, true},
  },
  {
    "h2_uds",
    /* grpc_end2end_test_fixture_options */
    {true, false, false, true, true, false, false, true, false, true, true},
  },
  {
    "inproc",
    /* grpc_end2end_test_fixture_options */
    {false, false, false, false, true, false, true, false, false, false, false},
  },
};

void grpc_end2end_tests_pre_init(void) {
  GPR_ASSERT(!g_pre_init_called);
  g_pre_init_called = true;
  for (int i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    configs[i].pre_init_func();
  }
}

void grpc_end2end_tests_run_single(grpc_end2end_test_config config, const char* test_name) {
  GPR_ASSERT(g_pre_init_called);
  
  for (int i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    if (0 == strcmp(configs[i].name, test_name)) {
      configs[i].test_func(config);
      return;
    }
  }
  gpr_log(GPR_DEBUG, "not a test: '%s'", test_name);
  abort();
}

// NOLINTNEXTLINE(readability-function-size)
void grpc_end2end_tests(int argc, char **argv,
                        grpc_end2end_test_config config) {
  int i;

  GPR_ASSERT(g_pre_init_called);

  if (argc <= 1) {
    for (int i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
      configs[i].test_func(config);
    }
    return;
  }

  for (i = 1; i < argc; i++) {
    grpc_end2end_tests_run_single(config, argv[i]);
  }
}

static bool is_compatible(const grpc_end2end_test_fixture_options& fixture, const grpc_end2end_test_case_options& test) {
    if (test.needs_fullstack && !fixture.fullstack) {
      return false;
    }
    if (test.needs_dns && !fixture.dns_resolver) {
      return false;
    }
    if (test.needs_names && !fixture.name_resolution) {
      return false;
    }
    if (!test.proxyable && fixture.includes_proxy) {
      return false;
    }
    if (!test.traceable && fixture.tracing) {
      return false;
    }
    if (test.exclude_inproc && fixture.is_inproc) {
      return false;
    }
    if (test.needs_http2 && !fixture.is_http2) {
      return false;
    }
    if (test.needs_proxy_auth && !fixture.supports_proxy_auth) {
      return false;
    }
    if (test.needs_write_buffering && !fixture.supports_write_buffering) {
      return false;
    }
    if (test.needs_client_channel && !fixture.client_channel) {
      return false;
    }
    return true;
}

static grpc_end2end_test_fixture_options get_fixture_options_by_name(const char* fixture_name) {
  for (int i = 0; i < sizeof(fixture_configs) / sizeof(*fixture_configs); i++) {
    if (0 == strcmp(fixture_configs[i].name, fixture_name)) {
      return fixture_configs[i].options;
    }
  }
  gpr_log(GPR_DEBUG, "not a fixture: '%s'", fixture_name);
  abort();
}

static std::vector<std::string> get_compatible_test_names(const char* fixture_name) {
  auto fixture_options = get_fixture_options_by_name(fixture_name);
  
  std::vector<std::string> result;
  for (int i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    if (is_compatible(fixture_options, configs[i].options)) {
      result.emplace_back(configs[i].name);
    }
  }
  return result;
}

std::vector<grpc::testing::CoreEnd2EndTestScenario> grpc::testing::CoreEnd2EndTestScenario::CreateTestScenarios(const char* fixture_name, grpc_end2end_test_config* configs, int num_configs)
{
  std::vector<CoreEnd2EndTestScenario> scenarios;
  auto test_names = get_compatible_test_names(fixture_name);

  for (int i = 0; i < test_names.size(); i++) {
    for (int j = 0; j < num_configs; j++) {
      scenarios.emplace_back(
          CoreEnd2EndTestScenario(configs[j], j, num_configs, test_names[i]));
    }
  }
  return scenarios;
}

