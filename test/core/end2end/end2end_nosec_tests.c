
/*
 *
 * Copyright 2015-2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* This file is auto-generated */

#include "test/core/end2end/end2end_tests.h"
#include <string.h>
#include <grpc/support/log.h>

extern void bad_hostname(grpc_end2end_test_config config);
extern void binary_metadata(grpc_end2end_test_config config);
extern void cancel_after_accept(grpc_end2end_test_config config);
extern void cancel_after_client_done(grpc_end2end_test_config config);
extern void cancel_after_invoke(grpc_end2end_test_config config);
extern void cancel_before_invoke(grpc_end2end_test_config config);
extern void cancel_in_a_vacuum(grpc_end2end_test_config config);
extern void cancel_with_status(grpc_end2end_test_config config);
extern void compressed_payload(grpc_end2end_test_config config);
extern void connectivity(grpc_end2end_test_config config);
extern void default_host(grpc_end2end_test_config config);
extern void disappearing_server(grpc_end2end_test_config config);
extern void empty_batch(grpc_end2end_test_config config);
extern void graceful_server_shutdown(grpc_end2end_test_config config);
extern void high_initial_seqno(grpc_end2end_test_config config);
extern void hpack_size(grpc_end2end_test_config config);
extern void invoke_large_request(grpc_end2end_test_config config);
extern void large_metadata(grpc_end2end_test_config config);
extern void max_concurrent_streams(grpc_end2end_test_config config);
extern void max_message_length(grpc_end2end_test_config config);
extern void negative_deadline(grpc_end2end_test_config config);
extern void no_op(grpc_end2end_test_config config);
extern void payload(grpc_end2end_test_config config);
extern void ping(grpc_end2end_test_config config);
extern void ping_pong_streaming(grpc_end2end_test_config config);
extern void registered_call(grpc_end2end_test_config config);
extern void request_with_flags(grpc_end2end_test_config config);
extern void request_with_payload(grpc_end2end_test_config config);
extern void server_finishes_request(grpc_end2end_test_config config);
extern void shutdown_finishes_calls(grpc_end2end_test_config config);
extern void shutdown_finishes_tags(grpc_end2end_test_config config);
extern void simple_delayed_request(grpc_end2end_test_config config);
extern void simple_metadata(grpc_end2end_test_config config);
extern void simple_request(grpc_end2end_test_config config);
extern void trailing_metadata(grpc_end2end_test_config config);

void grpc_end2end_tests(int argc, char **argv,
                        grpc_end2end_test_config config) {
  int i;

  if (argc <= 1) {
    bad_hostname(config);
    binary_metadata(config);
    cancel_after_accept(config);
    cancel_after_client_done(config);
    cancel_after_invoke(config);
    cancel_before_invoke(config);
    cancel_in_a_vacuum(config);
    cancel_with_status(config);
    compressed_payload(config);
    connectivity(config);
    default_host(config);
    disappearing_server(config);
    empty_batch(config);
    graceful_server_shutdown(config);
    high_initial_seqno(config);
    hpack_size(config);
    invoke_large_request(config);
    large_metadata(config);
    max_concurrent_streams(config);
    max_message_length(config);
    negative_deadline(config);
    no_op(config);
    payload(config);
    ping(config);
    ping_pong_streaming(config);
    registered_call(config);
    request_with_flags(config);
    request_with_payload(config);
    server_finishes_request(config);
    shutdown_finishes_calls(config);
    shutdown_finishes_tags(config);
    simple_delayed_request(config);
    simple_metadata(config);
    simple_request(config);
    trailing_metadata(config);
    return;
  }

  for (i = 1; i < argc; i++) {
    if (0 == strcmp("bad_hostname", argv[i])) {
      bad_hostname(config);
      continue;
    }
    if (0 == strcmp("binary_metadata", argv[i])) {
      binary_metadata(config);
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
    if (0 == strcmp("invoke_large_request", argv[i])) {
      invoke_large_request(config);
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
    if (0 == strcmp("max_message_length", argv[i])) {
      max_message_length(config);
      continue;
    }
    if (0 == strcmp("negative_deadline", argv[i])) {
      negative_deadline(config);
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
    if (0 == strcmp("trailing_metadata", argv[i])) {
      trailing_metadata(config);
      continue;
    }
    gpr_log(GPR_DEBUG, "not a test: '%s'", argv[i]);
    abort();
  }
}
