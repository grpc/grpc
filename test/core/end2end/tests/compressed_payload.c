/*
 *
 * Copyright 2015, Google Inc.
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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/compression.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/call_test_only.h"
#include "test/core/end2end/cq_verifier.h"

static void *tag(intptr_t t) { return (void *)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char *test_name,
                                            grpc_channel_args *client_args,
                                            grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "%s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
  config.init_server(&f, server_args);
  config.init_client(&f, client_args);
  return f;
}

static gpr_timespec n_seconds_time(int n) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(n);
}

static gpr_timespec five_seconds_time(void) { return n_seconds_time(5); }

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_time(), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture *f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(f->server, f->cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(
                 f->cq, tag(1000), GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5), NULL)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(f->server);
  f->server = NULL;
}

static void shutdown_client(grpc_end2end_test_fixture *f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = NULL;
}

static void end_test(grpc_end2end_test_fixture *f) {
  shutdown_server(f);
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
}

static void request_for_disabled_algorithm(
    grpc_end2end_test_config config, const char *test_name,
    uint32_t send_flags_bitmask,
    grpc_compression_algorithm algorithm_to_disable,
    grpc_compression_algorithm requested_client_compression_algorithm,
    grpc_status_code expected_error, grpc_metadata *client_metadata) {
  grpc_call *c;
  grpc_call *s;
  grpc_slice request_payload_slice;
  grpc_byte_buffer *request_payload;
  gpr_timespec deadline = five_seconds_time();
  grpc_channel_args *client_args;
  grpc_channel_args *server_args;
  grpc_end2end_test_fixture f;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  char *details = NULL;
  size_t details_capacity = 0;
  int was_cancelled = 2;
  cq_verifier *cqv;
  char str[1024];

  memset(str, 'x', 1023);
  str[1023] = '\0';
  request_payload_slice = grpc_slice_from_copied_string(str);
  request_payload = grpc_raw_byte_buffer_create(&request_payload_slice, 1);

  client_args = grpc_channel_args_set_compression_algorithm(
      NULL, requested_client_compression_algorithm);
  server_args =
      grpc_channel_args_set_compression_algorithm(NULL, GRPC_COMPRESS_NONE);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    server_args = grpc_channel_args_compression_algorithm_set_state(
        &exec_ctx, &server_args, algorithm_to_disable, false);
    grpc_exec_ctx_finish(&exec_ctx);
  }

  f = begin_test(config, test_name, client_args, server_args);
  cqv = cq_verifier_create(f.cq);

  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq, "/foo",
      get_host_override_string("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  if (client_metadata != NULL) {
    op->data.send_initial_metadata.count = 1;
    op->data.send_initial_metadata.metadata = client_metadata;
  } else {
    op->data.send_initial_metadata.count = 0;
  }
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = send_flags_bitmask;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), false);

  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(103), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(103), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  /* call was cancelled (closed) ... */
  GPR_ASSERT(was_cancelled != 0);
  /* with a certain error */
  GPR_ASSERT(status == expected_error);

  char *algo_name = NULL;
  GPR_ASSERT(grpc_compression_algorithm_name(algorithm_to_disable, &algo_name));
  char *expected_details = NULL;
  gpr_asprintf(&expected_details, "Compression algorithm '%s' is disabled.",
               algo_name);
  /* and we expect a specific reason for it */
  GPR_ASSERT(0 == strcmp(details, expected_details));
  gpr_free(expected_details);
  GPR_ASSERT(0 == strcmp(call_details.method, "/foo"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);

  gpr_free(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  cq_verifier_destroy(cqv);

  grpc_slice_unref(request_payload_slice);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(request_payload_recv);

  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_channel_args_destroy(&exec_ctx, client_args);
    grpc_channel_args_destroy(&exec_ctx, server_args);
    grpc_exec_ctx_finish(&exec_ctx);
  }

  end_test(&f);
  config.tear_down_data(&f);
}

static void request_with_payload_template(
    grpc_end2end_test_config config, const char *test_name,
    uint32_t client_send_flags_bitmask,
    grpc_compression_algorithm default_client_channel_compression_algorithm,
    grpc_compression_algorithm default_server_channel_compression_algorithm,
    grpc_compression_algorithm expected_algorithm_from_client,
    grpc_compression_algorithm expected_algorithm_from_server,
    grpc_metadata *client_init_metadata, bool set_server_level,
    grpc_compression_level server_compression_level) {
  grpc_call *c;
  grpc_call *s;
  grpc_slice request_payload_slice;
  grpc_byte_buffer *request_payload;
  gpr_timespec deadline = five_seconds_time();
  grpc_channel_args *client_args;
  grpc_channel_args *server_args;
  grpc_end2end_test_fixture f;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload;
  grpc_byte_buffer *response_payload_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  char *details = NULL;
  size_t details_capacity = 0;
  int was_cancelled = 2;
  cq_verifier *cqv;
  char request_str[1024];
  char response_str[1024];

  memset(request_str, 'x', 1023);
  request_str[1023] = '\0';

  memset(response_str, 'y', 1023);
  response_str[1023] = '\0';

  request_payload_slice = grpc_slice_from_copied_string(request_str);
  grpc_slice response_payload_slice =
      grpc_slice_from_copied_string(response_str);

  client_args = grpc_channel_args_set_compression_algorithm(
      NULL, default_client_channel_compression_algorithm);
  server_args = grpc_channel_args_set_compression_algorithm(
      NULL, default_server_channel_compression_algorithm);

  f = begin_test(config, test_name, client_args, server_args);
  cqv = cq_verifier_create(f.cq);

  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq, "/foo",
      get_host_override_string("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  if (client_init_metadata != NULL) {
    op->data.send_initial_metadata.count = 1;
    op->data.send_initial_metadata.metadata = client_init_metadata;
  } else {
    op->data.send_initial_metadata.count = 0;
  }
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(100));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(100), true);
  cq_verify(cqv);

  GPR_ASSERT(GPR_BITCOUNT(grpc_call_test_only_get_encodings_accepted_by_peer(
                 s)) == GRPC_COMPRESS_ALGORITHMS_COUNT);
  GPR_ASSERT(GPR_BITGET(grpc_call_test_only_get_encodings_accepted_by_peer(s),
                        GRPC_COMPRESS_NONE) != 0);
  GPR_ASSERT(GPR_BITGET(grpc_call_test_only_get_encodings_accepted_by_peer(s),
                        GRPC_COMPRESS_DEFLATE) != 0);
  GPR_ASSERT(GPR_BITGET(grpc_call_test_only_get_encodings_accepted_by_peer(s),
                        GRPC_COMPRESS_GZIP) != 0);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  if (set_server_level) {
    op->data.send_initial_metadata.maybe_compression_level.is_set = true;
    op->data.send_initial_metadata.maybe_compression_level.level =
        server_compression_level;
  }
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(101), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  for (int i = 0; i < 2; i++) {
    request_payload = grpc_raw_byte_buffer_create(&request_payload_slice, 1);
    response_payload = grpc_raw_byte_buffer_create(&response_payload_slice, 1);

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = request_payload;
    op->flags = client_send_flags_bitmask;
    op->reserved = NULL;
    op++;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload_recv;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(2), NULL);
    GPR_ASSERT(GRPC_CALL_OK == error);

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &request_payload_recv;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
    GPR_ASSERT(GRPC_CALL_OK == error);
    CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
    cq_verify(cqv);

    GPR_ASSERT(request_payload_recv->type == GRPC_BB_RAW);
    GPR_ASSERT(byte_buffer_eq_string(request_payload_recv, request_str));
    GPR_ASSERT(request_payload_recv->data.raw.compression ==
               expected_algorithm_from_client);

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = response_payload;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(103), NULL);
    GPR_ASSERT(GRPC_CALL_OK == error);
    CQ_EXPECT_COMPLETION(cqv, tag(103), 1);
    CQ_EXPECT_COMPLETION(cqv, tag(2), 1);
    cq_verify(cqv);

    GPR_ASSERT(response_payload_recv->type == GRPC_BB_RAW);
    GPR_ASSERT(byte_buffer_eq_string(response_payload_recv, response_str));
    if (server_compression_level > GRPC_COMPRESS_LEVEL_NONE) {
      const grpc_compression_algorithm algo_for_server_level =
          grpc_call_compression_for_level(s, server_compression_level);
      GPR_ASSERT(response_payload_recv->data.raw.compression ==
                 algo_for_server_level);
    } else {
      GPR_ASSERT(response_payload_recv->data.raw.compression ==
                 expected_algorithm_from_server);
    }

    grpc_byte_buffer_destroy(request_payload);
    grpc_byte_buffer_destroy(response_payload);
    grpc_byte_buffer_destroy(request_payload_recv);
    grpc_byte_buffer_destroy(response_payload_recv);
  }

  grpc_slice_unref(request_payload_slice);
  grpc_slice_unref(response_payload_slice);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(3), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->data.send_status_from_server.status_details = "xyz";
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(104), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(3), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(104), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_OK);
  GPR_ASSERT(0 == strcmp(details, "xyz"));
  GPR_ASSERT(0 == strcmp(call_details.method, "/foo"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(was_cancelled == 0);

  gpr_free(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  cq_verifier_destroy(cqv);

  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_channel_args_destroy(&exec_ctx, client_args);
    grpc_channel_args_destroy(&exec_ctx, server_args);
    grpc_exec_ctx_finish(&exec_ctx);
  }

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_invoke_request_with_exceptionally_uncompressed_payload(
    grpc_end2end_test_config config) {
  request_with_payload_template(
      config, "test_invoke_request_with_exceptionally_uncompressed_payload",
      GRPC_WRITE_NO_COMPRESS, GRPC_COMPRESS_GZIP, GRPC_COMPRESS_GZIP,
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_GZIP, NULL, false,
      /* ignored */ GRPC_COMPRESS_LEVEL_NONE);
}

static void test_invoke_request_with_uncompressed_payload(
    grpc_end2end_test_config config) {
  request_with_payload_template(
      config, "test_invoke_request_with_uncompressed_payload", 0,
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE,
      GRPC_COMPRESS_NONE, NULL, false, /* ignored */ GRPC_COMPRESS_LEVEL_NONE);
}

static void test_invoke_request_with_compressed_payload(
    grpc_end2end_test_config config) {
  request_with_payload_template(
      config, "test_invoke_request_with_compressed_payload", 0,
      GRPC_COMPRESS_GZIP, GRPC_COMPRESS_GZIP, GRPC_COMPRESS_GZIP,
      GRPC_COMPRESS_GZIP, NULL, false, /* ignored */ GRPC_COMPRESS_LEVEL_NONE);
}

static void test_invoke_request_with_server_level(
    grpc_end2end_test_config config) {
  request_with_payload_template(
      config, "test_invoke_request_with_server_level", 0, GRPC_COMPRESS_NONE,
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE /* ignored */,
      NULL, true, GRPC_COMPRESS_LEVEL_HIGH);
}

static void test_invoke_request_with_compressed_payload_md_override(
    grpc_end2end_test_config config) {
  grpc_metadata gzip_compression_override;
  grpc_metadata identity_compression_override;

  gzip_compression_override.key = GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY;
  gzip_compression_override.value = "gzip";
  gzip_compression_override.value_length =
      strlen(gzip_compression_override.value);
  memset(&gzip_compression_override.internal_data, 0,
         sizeof(gzip_compression_override.internal_data));

  identity_compression_override.key = GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY;
  identity_compression_override.value = "identity";
  identity_compression_override.value_length =
      strlen(identity_compression_override.value);
  memset(&identity_compression_override.internal_data, 0,
         sizeof(identity_compression_override.internal_data));

  /* Channel default NONE (aka IDENTITY), call override to GZIP */
  request_with_payload_template(
      config, "test_invoke_request_with_compressed_payload_md_override_1", 0,
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE, GRPC_COMPRESS_GZIP,
      GRPC_COMPRESS_NONE, &gzip_compression_override, false,
      /*ignored*/ GRPC_COMPRESS_LEVEL_NONE);

  /* Channel default DEFLATE, call override to GZIP */
  request_with_payload_template(
      config, "test_invoke_request_with_compressed_payload_md_override_2", 0,
      GRPC_COMPRESS_DEFLATE, GRPC_COMPRESS_NONE, GRPC_COMPRESS_GZIP,
      GRPC_COMPRESS_NONE, &gzip_compression_override, false,
      /*ignored*/ GRPC_COMPRESS_LEVEL_NONE);

  /* Channel default DEFLATE, call override to NONE (aka IDENTITY) */
  request_with_payload_template(
      config, "test_invoke_request_with_compressed_payload_md_override_3", 0,
      GRPC_COMPRESS_DEFLATE, GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE,
      GRPC_COMPRESS_NONE, &identity_compression_override, false,
      /*ignored*/ GRPC_COMPRESS_LEVEL_NONE);
}

static void test_invoke_request_with_disabled_algorithm(
    grpc_end2end_test_config config) {
  request_for_disabled_algorithm(
      config, "test_invoke_request_with_disabled_algorithm", 0,
      GRPC_COMPRESS_GZIP, GRPC_COMPRESS_GZIP, GRPC_STATUS_UNIMPLEMENTED, NULL);
}

void compressed_payload(grpc_end2end_test_config config) {
  test_invoke_request_with_exceptionally_uncompressed_payload(config);
  test_invoke_request_with_uncompressed_payload(config);
  test_invoke_request_with_compressed_payload(config);
  test_invoke_request_with_server_level(config);
  test_invoke_request_with_compressed_payload_md_override(config);
  test_invoke_request_with_disabled_algorithm(config);
}

void compressed_payload_pre_init(void) {}
