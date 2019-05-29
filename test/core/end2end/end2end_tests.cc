
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
extern void filter_call_init_fails(grpc_end2end_test_config config);
extern void filter_call_init_fails_pre_init(void);
extern void filter_causes_close(grpc_end2end_test_config config);
extern void filter_causes_close_pre_init(void);
extern void filter_context(grpc_end2end_test_config config);
extern void filter_context_pre_init(void);
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
extern void retry_cancellation(grpc_end2end_test_config config);
extern void retry_cancellation_pre_init(void);
extern void retry_disabled(grpc_end2end_test_config config);
extern void retry_disabled_pre_init(void);
extern void retry_exceeds_buffer_size_in_initial_batch(grpc_end2end_test_config config);
extern void retry_exceeds_buffer_size_in_initial_batch_pre_init(void);
extern void retry_exceeds_buffer_size_in_subsequent_batch(grpc_end2end_test_config config);
extern void retry_exceeds_buffer_size_in_subsequent_batch_pre_init(void);
extern void retry_non_retriable_status(grpc_end2end_test_config config);
extern void retry_non_retriable_status_pre_init(void);
extern void retry_non_retriable_status_before_recv_trailing_metadata_started(grpc_end2end_test_config config);
extern void retry_non_retriable_status_before_recv_trailing_metadata_started_pre_init(void);
extern void retry_recv_initial_metadata(grpc_end2end_test_config config);
extern void retry_recv_initial_metadata_pre_init(void);
extern void retry_recv_message(grpc_end2end_test_config config);
extern void retry_recv_message_pre_init(void);
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
extern void server_finishes_request(grpc_end2end_test_config config);
extern void server_finishes_request_pre_init(void);
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
extern void workaround_cronet_compression(grpc_end2end_test_config config);
extern void workaround_cronet_compression_pre_init(void);
extern void write_buffering(grpc_end2end_test_config config);
extern void write_buffering_pre_init(void);
extern void write_buffering_at_end(grpc_end2end_test_config config);
extern void write_buffering_at_end_pre_init(void);

void grpc_end2end_tests_pre_init(void) {
  GPR_ASSERT(!g_pre_init_called);
  g_pre_init_called = true;
  authority_not_supported_pre_init();
  bad_hostname_pre_init();
  bad_ping_pre_init();
  binary_metadata_pre_init();
  call_creds_pre_init();
  call_host_override_pre_init();
  cancel_after_accept_pre_init();
  cancel_after_client_done_pre_init();
  cancel_after_invoke_pre_init();
  cancel_after_round_trip_pre_init();
  cancel_before_invoke_pre_init();
  cancel_in_a_vacuum_pre_init();
  cancel_with_status_pre_init();
  channelz_pre_init();
  compressed_payload_pre_init();
  connectivity_pre_init();
  default_host_pre_init();
  disappearing_server_pre_init();
  empty_batch_pre_init();
  filter_call_init_fails_pre_init();
  filter_causes_close_pre_init();
  filter_context_pre_init();
  filter_latency_pre_init();
  filter_status_code_pre_init();
  graceful_server_shutdown_pre_init();
  high_initial_seqno_pre_init();
  hpack_size_pre_init();
  idempotent_request_pre_init();
  invoke_large_request_pre_init();
  keepalive_timeout_pre_init();
  large_metadata_pre_init();
  max_concurrent_streams_pre_init();
  max_connection_age_pre_init();
  max_connection_idle_pre_init();
  max_message_length_pre_init();
  negative_deadline_pre_init();
  no_error_on_hotpath_pre_init();
  no_logging_pre_init();
  no_op_pre_init();
  payload_pre_init();
  ping_pre_init();
  ping_pong_streaming_pre_init();
  proxy_auth_pre_init();
  registered_call_pre_init();
  request_with_flags_pre_init();
  request_with_payload_pre_init();
  resource_quota_server_pre_init();
  retry_pre_init();
  retry_cancellation_pre_init();
  retry_disabled_pre_init();
  retry_exceeds_buffer_size_in_initial_batch_pre_init();
  retry_exceeds_buffer_size_in_subsequent_batch_pre_init();
  retry_non_retriable_status_pre_init();
  retry_non_retriable_status_before_recv_trailing_metadata_started_pre_init();
  retry_recv_initial_metadata_pre_init();
  retry_recv_message_pre_init();
  retry_server_pushback_delay_pre_init();
  retry_server_pushback_disabled_pre_init();
  retry_streaming_pre_init();
  retry_streaming_after_commit_pre_init();
  retry_streaming_succeeds_before_replay_finished_pre_init();
  retry_throttled_pre_init();
  retry_too_many_attempts_pre_init();
  server_finishes_request_pre_init();
  shutdown_finishes_calls_pre_init();
  shutdown_finishes_tags_pre_init();
  simple_cacheable_request_pre_init();
  simple_delayed_request_pre_init();
  simple_metadata_pre_init();
  simple_request_pre_init();
  stream_compression_compressed_payload_pre_init();
  stream_compression_payload_pre_init();
  stream_compression_ping_pong_streaming_pre_init();
  streaming_error_response_pre_init();
  trailing_metadata_pre_init();
  workaround_cronet_compression_pre_init();
  write_buffering_pre_init();
  write_buffering_at_end_pre_init();
}

void grpc_end2end_tests(int argc, char **argv,
                        grpc_end2end_test_config config) {
  int i;

  GPR_ASSERT(g_pre_init_called);

  if (argc <= 1) {
    authority_not_supported(config);
    bad_hostname(config);
    bad_ping(config);
    binary_metadata(config);
    call_creds(config);
    call_host_override(config);
    cancel_after_accept(config);
    cancel_after_client_done(config);
    cancel_after_invoke(config);
    cancel_after_round_trip(config);
    cancel_before_invoke(config);
    cancel_in_a_vacuum(config);
    cancel_with_status(config);
    channelz(config);
    compressed_payload(config);
    connectivity(config);
    default_host(config);
    disappearing_server(config);
    empty_batch(config);
    filter_call_init_fails(config);
    filter_causes_close(config);
    filter_context(config);
    filter_latency(config);
    filter_status_code(config);
    graceful_server_shutdown(config);
    high_initial_seqno(config);
    hpack_size(config);
    idempotent_request(config);
    invoke_large_request(config);
    keepalive_timeout(config);
    large_metadata(config);
    max_concurrent_streams(config);
    max_connection_age(config);
    max_connection_idle(config);
    max_message_length(config);
    negative_deadline(config);
    no_error_on_hotpath(config);
    no_logging(config);
    no_op(config);
    payload(config);
    ping(config);
    ping_pong_streaming(config);
    proxy_auth(config);
    registered_call(config);
    request_with_flags(config);
    request_with_payload(config);
    resource_quota_server(config);
    retry(config);
    retry_cancellation(config);
    retry_disabled(config);
    retry_exceeds_buffer_size_in_initial_batch(config);
    retry_exceeds_buffer_size_in_subsequent_batch(config);
    retry_non_retriable_status(config);
    retry_non_retriable_status_before_recv_trailing_metadata_started(config);
    retry_recv_initial_metadata(config);
    retry_recv_message(config);
    retry_server_pushback_delay(config);
    retry_server_pushback_disabled(config);
    retry_streaming(config);
    retry_streaming_after_commit(config);
    retry_streaming_succeeds_before_replay_finished(config);
    retry_throttled(config);
    retry_too_many_attempts(config);
    server_finishes_request(config);
    shutdown_finishes_calls(config);
    shutdown_finishes_tags(config);
    simple_cacheable_request(config);
    simple_delayed_request(config);
    simple_metadata(config);
    simple_request(config);
    stream_compression_compressed_payload(config);
    stream_compression_payload(config);
    stream_compression_ping_pong_streaming(config);
    streaming_error_response(config);
    trailing_metadata(config);
    workaround_cronet_compression(config);
    write_buffering(config);
    write_buffering_at_end(config);
    return;
  }

  for (i = 1; i < argc; i++) {
    if (0 == strcmp("authority_not_supported", argv[i])) {
      authority_not_supported(config);
      continue;
    }
    if (0 == strcmp("bad_hostname", argv[i])) {
      bad_hostname(config);
      continue;
    }
    if (0 == strcmp("bad_ping", argv[i])) {
      bad_ping(config);
      continue;
    }
    if (0 == strcmp("binary_metadata", argv[i])) {
      binary_metadata(config);
      continue;
    }
    if (0 == strcmp("call_creds", argv[i])) {
      call_creds(config);
      continue;
    }
    if (0 == strcmp("call_host_override", argv[i])) {
      call_host_override(config);
      continue;
    }
    if (0 == strcmp("cancel_after_accept", argv[i])) {
      cancel_after_accept(config);
      continue;
    }
    if (0 == strcmp("cancel_after_client_done", argv[i])) {
      cancel_after_client_done(config);
      continue;
    }
    if (0 == strcmp("cancel_after_invoke", argv[i])) {
      cancel_after_invoke(config);
      continue;
    }
    if (0 == strcmp("cancel_after_round_trip", argv[i])) {
      cancel_after_round_trip(config);
      continue;
    }
    if (0 == strcmp("cancel_before_invoke", argv[i])) {
      cancel_before_invoke(config);
      continue;
    }
    if (0 == strcmp("cancel_in_a_vacuum", argv[i])) {
      cancel_in_a_vacuum(config);
      continue;
    }
    if (0 == strcmp("cancel_with_status", argv[i])) {
      cancel_with_status(config);
      continue;
    }
    if (0 == strcmp("channelz", argv[i])) {
      channelz(config);
      continue;
    }
    if (0 == strcmp("compressed_payload", argv[i])) {
      compressed_payload(config);
      continue;
    }
    if (0 == strcmp("connectivity", argv[i])) {
      connectivity(config);
      continue;
    }
    if (0 == strcmp("default_host", argv[i])) {
      default_host(config);
      continue;
    }
    if (0 == strcmp("disappearing_server", argv[i])) {
      disappearing_server(config);
      continue;
    }
    if (0 == strcmp("empty_batch", argv[i])) {
      empty_batch(config);
      continue;
    }
    if (0 == strcmp("filter_call_init_fails", argv[i])) {
      filter_call_init_fails(config);
      continue;
    }
    if (0 == strcmp("filter_causes_close", argv[i])) {
      filter_causes_close(config);
      continue;
    }
    if (0 == strcmp("filter_context", argv[i])) {
      filter_context(config);
      continue;
    }
    if (0 == strcmp("filter_latency", argv[i])) {
      filter_latency(config);
      continue;
    }
    if (0 == strcmp("filter_status_code", argv[i])) {
      filter_status_code(config);
      continue;
    }
    if (0 == strcmp("graceful_server_shutdown", argv[i])) {
      graceful_server_shutdown(config);
      continue;
    }
    if (0 == strcmp("high_initial_seqno", argv[i])) {
      high_initial_seqno(config);
      continue;
    }
    if (0 == strcmp("hpack_size", argv[i])) {
      hpack_size(config);
      continue;
    }
    if (0 == strcmp("idempotent_request", argv[i])) {
      idempotent_request(config);
      continue;
    }
    if (0 == strcmp("invoke_large_request", argv[i])) {
      invoke_large_request(config);
      continue;
    }
    if (0 == strcmp("keepalive_timeout", argv[i])) {
      keepalive_timeout(config);
      continue;
    }
    if (0 == strcmp("large_metadata", argv[i])) {
      large_metadata(config);
      continue;
    }
    if (0 == strcmp("max_concurrent_streams", argv[i])) {
      max_concurrent_streams(config);
      continue;
    }
    if (0 == strcmp("max_connection_age", argv[i])) {
      max_connection_age(config);
      continue;
    }
    if (0 == strcmp("max_connection_idle", argv[i])) {
      max_connection_idle(config);
      continue;
    }
    if (0 == strcmp("max_message_length", argv[i])) {
      max_message_length(config);
      continue;
    }
    if (0 == strcmp("negative_deadline", argv[i])) {
      negative_deadline(config);
      continue;
    }
    if (0 == strcmp("no_error_on_hotpath", argv[i])) {
      no_error_on_hotpath(config);
      continue;
    }
    if (0 == strcmp("no_logging", argv[i])) {
      no_logging(config);
      continue;
    }
    if (0 == strcmp("no_op", argv[i])) {
      no_op(config);
      continue;
    }
    if (0 == strcmp("payload", argv[i])) {
      payload(config);
      continue;
    }
    if (0 == strcmp("ping", argv[i])) {
      ping(config);
      continue;
    }
    if (0 == strcmp("ping_pong_streaming", argv[i])) {
      ping_pong_streaming(config);
      continue;
    }
    if (0 == strcmp("proxy_auth", argv[i])) {
      proxy_auth(config);
      continue;
    }
    if (0 == strcmp("registered_call", argv[i])) {
      registered_call(config);
      continue;
    }
    if (0 == strcmp("request_with_flags", argv[i])) {
      request_with_flags(config);
      continue;
    }
    if (0 == strcmp("request_with_payload", argv[i])) {
      request_with_payload(config);
      continue;
    }
    if (0 == strcmp("resource_quota_server", argv[i])) {
      resource_quota_server(config);
      continue;
    }
    if (0 == strcmp("retry", argv[i])) {
      retry(config);
      continue;
    }
    if (0 == strcmp("retry_cancellation", argv[i])) {
      retry_cancellation(config);
      continue;
    }
    if (0 == strcmp("retry_disabled", argv[i])) {
      retry_disabled(config);
      continue;
    }
    if (0 == strcmp("retry_exceeds_buffer_size_in_initial_batch", argv[i])) {
      retry_exceeds_buffer_size_in_initial_batch(config);
      continue;
    }
    if (0 == strcmp("retry_exceeds_buffer_size_in_subsequent_batch", argv[i])) {
      retry_exceeds_buffer_size_in_subsequent_batch(config);
      continue;
    }
    if (0 == strcmp("retry_non_retriable_status", argv[i])) {
      retry_non_retriable_status(config);
      continue;
    }
    if (0 == strcmp("retry_non_retriable_status_before_recv_trailing_metadata_started", argv[i])) {
      retry_non_retriable_status_before_recv_trailing_metadata_started(config);
      continue;
    }
    if (0 == strcmp("retry_recv_initial_metadata", argv[i])) {
      retry_recv_initial_metadata(config);
      continue;
    }
    if (0 == strcmp("retry_recv_message", argv[i])) {
      retry_recv_message(config);
      continue;
    }
    if (0 == strcmp("retry_server_pushback_delay", argv[i])) {
      retry_server_pushback_delay(config);
      continue;
    }
    if (0 == strcmp("retry_server_pushback_disabled", argv[i])) {
      retry_server_pushback_disabled(config);
      continue;
    }
    if (0 == strcmp("retry_streaming", argv[i])) {
      retry_streaming(config);
      continue;
    }
    if (0 == strcmp("retry_streaming_after_commit", argv[i])) {
      retry_streaming_after_commit(config);
      continue;
    }
    if (0 == strcmp("retry_streaming_succeeds_before_replay_finished", argv[i])) {
      retry_streaming_succeeds_before_replay_finished(config);
      continue;
    }
    if (0 == strcmp("retry_throttled", argv[i])) {
      retry_throttled(config);
      continue;
    }
    if (0 == strcmp("retry_too_many_attempts", argv[i])) {
      retry_too_many_attempts(config);
      continue;
    }
    if (0 == strcmp("server_finishes_request", argv[i])) {
      server_finishes_request(config);
      continue;
    }
    if (0 == strcmp("shutdown_finishes_calls", argv[i])) {
      shutdown_finishes_calls(config);
      continue;
    }
    if (0 == strcmp("shutdown_finishes_tags", argv[i])) {
      shutdown_finishes_tags(config);
      continue;
    }
    if (0 == strcmp("simple_cacheable_request", argv[i])) {
      simple_cacheable_request(config);
      continue;
    }
    if (0 == strcmp("simple_delayed_request", argv[i])) {
      simple_delayed_request(config);
      continue;
    }
    if (0 == strcmp("simple_metadata", argv[i])) {
      simple_metadata(config);
      continue;
    }
    if (0 == strcmp("simple_request", argv[i])) {
      simple_request(config);
      continue;
    }
    if (0 == strcmp("stream_compression_compressed_payload", argv[i])) {
      stream_compression_compressed_payload(config);
      continue;
    }
    if (0 == strcmp("stream_compression_payload", argv[i])) {
      stream_compression_payload(config);
      continue;
    }
    if (0 == strcmp("stream_compression_ping_pong_streaming", argv[i])) {
      stream_compression_ping_pong_streaming(config);
      continue;
    }
    if (0 == strcmp("streaming_error_response", argv[i])) {
      streaming_error_response(config);
      continue;
    }
    if (0 == strcmp("trailing_metadata", argv[i])) {
      trailing_metadata(config);
      continue;
    }
    if (0 == strcmp("workaround_cronet_compression", argv[i])) {
      workaround_cronet_compression(config);
      continue;
    }
    if (0 == strcmp("write_buffering", argv[i])) {
      write_buffering(config);
      continue;
    }
    if (0 == strcmp("write_buffering_at_end", argv[i])) {
      write_buffering_at_end(config);
      continue;
    }
    gpr_log(GPR_DEBUG, "not a test: '%s'", argv[i]);
    abort();
  }
}
