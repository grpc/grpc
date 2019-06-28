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

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/compression/compression_args.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/call_test_only.h"
#include "src/core/lib/transport/static_metadata.h"
#include "test/core/end2end/cq_verifier.h"

static void* tag(intptr_t t) { return (void*)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char* test_name,
                                            grpc_channel_args* client_args,
                                            grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
  config.init_server(&f, server_args);
  config.init_client(&f, client_args);
  return f;
}

static gpr_timespec n_seconds_from_now(int n) {
  return grpc_timeout_seconds_to_deadline(n);
}

static gpr_timespec five_seconds_from_now(void) {
  return n_seconds_from_now(5);
}

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_from_now(), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture* f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(f->server, f->shutdown_cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(f->shutdown_cq, tag(1000),
                                         grpc_timeout_seconds_to_deadline(5),
                                         nullptr)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(f->server);
  f->server = nullptr;
}

static void shutdown_client(grpc_end2end_test_fixture* f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = nullptr;
}

static void end_test(grpc_end2end_test_fixture* f) {
  shutdown_server(f);
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
  grpc_completion_queue_destroy(f->shutdown_cq);
}

static void request_for_disabled_algorithm(
    grpc_end2end_test_config config, const char* test_name,
    uint32_t send_flags_bitmask,
    grpc_compression_algorithm algorithm_to_disable,
    grpc_compression_algorithm requested_client_compression_algorithm,
    grpc_status_code expected_error, grpc_metadata* client_metadata) {
  grpc_call* c;
  grpc_call* s;
  grpc_slice request_payload_slice;
  grpc_byte_buffer* request_payload;
  grpc_channel_args* client_args;
  grpc_channel_args* server_args;
  grpc_end2end_test_fixture f;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* request_payload_recv = nullptr;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  cq_verifier* cqv;
  char str[1024];

  memset(str, 'x', 1023);
  str[1023] = '\0';
  request_payload_slice = grpc_slice_from_copied_string(str);
  request_payload = grpc_raw_byte_buffer_create(&request_payload_slice, 1);

  client_args = grpc_channel_args_set_channel_default_compression_algorithm(
      nullptr, requested_client_compression_algorithm);
  server_args = grpc_channel_args_set_channel_default_compression_algorithm(
      nullptr, GRPC_COMPRESS_NONE);
  {
    grpc_core::ExecCtx exec_ctx;
    server_args = grpc_channel_args_compression_algorithm_set_state(
        &server_args, algorithm_to_disable, false);
  }

  f = begin_test(config, test_name, client_args, server_args);
  cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  if (client_metadata != nullptr) {
    op->data.send_initial_metadata.count = 1;
    op->data.send_initial_metadata.metadata = client_metadata;
  } else {
    op->data.send_initial_metadata.count = 0;
  }
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = send_flags_bitmask;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), false);

  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(103),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(103), true);
  cq_verify(cqv);

  /* call was cancelled (closed) ... */
  GPR_ASSERT(was_cancelled != 0);
  /* with a certain error */
  GPR_ASSERT(status == expected_error);

  const char* algo_name = nullptr;
  GPR_ASSERT(grpc_compression_algorithm_name(algorithm_to_disable, &algo_name));
  char* expected_details = nullptr;
  gpr_asprintf(&expected_details, "Compression algorithm '%s' is disabled.",
               algo_name);
  /* and we expect a specific reason for it */
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, expected_details));
  gpr_free(expected_details);
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);

  grpc_slice_unref(request_payload_slice);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(request_payload_recv);

  {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(client_args);
    grpc_channel_args_destroy(server_args);
  }

  end_test(&f);
  config.tear_down_data(&f);
}

static void request_with_payload_template(
    grpc_end2end_test_config config, const char* test_name,
    uint32_t client_send_flags_bitmask,
    grpc_compression_algorithm default_client_channel_compression_algorithm,
    grpc_compression_algorithm default_server_channel_compression_algorithm,
    grpc_compression_algorithm expected_algorithm_from_client,
    grpc_compression_algorithm expected_algorithm_from_server,
    grpc_metadata* client_init_metadata, bool set_server_level,
    grpc_compression_level server_compression_level,
    bool send_message_before_initial_metadata) {
  grpc_call* c;
  grpc_call* s;
  grpc_slice request_payload_slice;
  grpc_byte_buffer* request_payload = nullptr;
  grpc_channel_args* client_args;
  grpc_channel_args* server_args;
  grpc_end2end_test_fixture f;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* request_payload_recv = nullptr;
  grpc_byte_buffer* response_payload;
  grpc_byte_buffer* response_payload_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  cq_verifier* cqv;
  char request_str[1024];
  char response_str[1024];

  memset(request_str, 'x', 1023);
  request_str[1023] = '\0';

  memset(response_str, 'y', 1023);
  response_str[1023] = '\0';

  request_payload_slice = grpc_slice_from_copied_string(request_str);
  grpc_slice response_payload_slice =
      grpc_slice_from_copied_string(response_str);

  client_args = grpc_channel_args_set_channel_default_compression_algorithm(
      nullptr, default_client_channel_compression_algorithm);
  server_args = grpc_channel_args_set_channel_default_compression_algorithm(
      nullptr, default_server_channel_compression_algorithm);

  f = begin_test(config, test_name, client_args, server_args);
  cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  if (send_message_before_initial_metadata) {
    request_payload = grpc_raw_byte_buffer_create(&request_payload_slice, 1);
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = request_payload;
    op->flags = client_send_flags_bitmask;
    op->reserved = nullptr;
    op++;
    error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(2),
                                  nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);
    CQ_EXPECT_COMPLETION(cqv, tag(2), true);
  }

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  if (client_init_metadata != nullptr) {
    op->data.send_initial_metadata.count = 1;
    op->data.send_initial_metadata.metadata = client_init_metadata;
  } else {
    op->data.send_initial_metadata.count = 0;
  }
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
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
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(101),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  for (int i = 0; i < 2; i++) {
    response_payload = grpc_raw_byte_buffer_create(&response_payload_slice, 1);

    if (i > 0 || !send_message_before_initial_metadata) {
      request_payload = grpc_raw_byte_buffer_create(&request_payload_slice, 1);
      memset(ops, 0, sizeof(ops));
      op = ops;
      op->op = GRPC_OP_SEND_MESSAGE;
      op->data.send_message.send_message = request_payload;
      op->flags = client_send_flags_bitmask;
      op->reserved = nullptr;
      op++;
      error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                    tag(2), nullptr);
      GPR_ASSERT(GRPC_CALL_OK == error);
      CQ_EXPECT_COMPLETION(cqv, tag(2), 1);
    }

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &request_payload_recv;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                  tag(102), nullptr);
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
    op->reserved = nullptr;
    op++;
    error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                  tag(103), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload_recv;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(3),
                                  nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);

    CQ_EXPECT_COMPLETION(cqv, tag(103), 1);
    CQ_EXPECT_COMPLETION(cqv, tag(3), 1);
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
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(4),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(104),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(4), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(104), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_OK);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
  GPR_ASSERT(was_cancelled == 0);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);

  {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(client_args);
    grpc_channel_args_destroy(server_args);
  }

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_invoke_request_with_exceptionally_uncompressed_payload(
    grpc_end2end_test_config config) {
  request_with_payload_template(
      config, "test_invoke_request_with_exceptionally_uncompressed_payload",
      GRPC_WRITE_NO_COMPRESS, GRPC_COMPRESS_GZIP, GRPC_COMPRESS_GZIP,
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_GZIP, nullptr, false,
      /* ignored */ GRPC_COMPRESS_LEVEL_NONE, false);
}

static void test_invoke_request_with_uncompressed_payload(
    grpc_end2end_test_config config) {
  request_with_payload_template(
      config, "test_invoke_request_with_uncompressed_payload", 0,
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE,
      GRPC_COMPRESS_NONE, nullptr, false,
      /* ignored */ GRPC_COMPRESS_LEVEL_NONE, false);
}

static void test_invoke_request_with_compressed_payload(
    grpc_end2end_test_config config) {
  request_with_payload_template(
      config, "test_invoke_request_with_compressed_payload", 0,
      GRPC_COMPRESS_GZIP, GRPC_COMPRESS_GZIP, GRPC_COMPRESS_GZIP,
      GRPC_COMPRESS_GZIP, nullptr, false,
      /* ignored */ GRPC_COMPRESS_LEVEL_NONE, false);
}

static void test_invoke_request_with_send_message_before_initial_metadata(
    grpc_end2end_test_config config) {
  request_with_payload_template(
      config, "test_invoke_request_with_compressed_payload", 0,
      GRPC_COMPRESS_GZIP, GRPC_COMPRESS_GZIP, GRPC_COMPRESS_GZIP,
      GRPC_COMPRESS_GZIP, nullptr, false,
      /* ignored */ GRPC_COMPRESS_LEVEL_NONE, true);
}

static void test_invoke_request_with_server_level(
    grpc_end2end_test_config config) {
  request_with_payload_template(
      config, "test_invoke_request_with_server_level", 0, GRPC_COMPRESS_NONE,
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE /* ignored */,
      nullptr, true, GRPC_COMPRESS_LEVEL_HIGH, false);
}

static void test_invoke_request_with_compressed_payload_md_override(
    grpc_end2end_test_config config) {
  grpc_metadata gzip_compression_override;
  grpc_metadata identity_compression_override;

  gzip_compression_override.key = GRPC_MDSTR_GRPC_INTERNAL_ENCODING_REQUEST;
  gzip_compression_override.value = grpc_slice_from_static_string("gzip");
  memset(&gzip_compression_override.internal_data, 0,
         sizeof(gzip_compression_override.internal_data));

  identity_compression_override.key = GRPC_MDSTR_GRPC_INTERNAL_ENCODING_REQUEST;
  identity_compression_override.value =
      grpc_slice_from_static_string("identity");
  memset(&identity_compression_override.internal_data, 0,
         sizeof(identity_compression_override.internal_data));

  /* Channel default NONE (aka IDENTITY), call override to GZIP */
  request_with_payload_template(
      config, "test_invoke_request_with_compressed_payload_md_override_1", 0,
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE, GRPC_COMPRESS_GZIP,
      GRPC_COMPRESS_NONE, &gzip_compression_override, false,
      /*ignored*/ GRPC_COMPRESS_LEVEL_NONE, false);

  /* Channel default DEFLATE, call override to GZIP */
  request_with_payload_template(
      config, "test_invoke_request_with_compressed_payload_md_override_2", 0,
      GRPC_COMPRESS_DEFLATE, GRPC_COMPRESS_NONE, GRPC_COMPRESS_GZIP,
      GRPC_COMPRESS_NONE, &gzip_compression_override, false,
      /*ignored*/ GRPC_COMPRESS_LEVEL_NONE, false);

  /* Channel default DEFLATE, call override to NONE (aka IDENTITY) */
  request_with_payload_template(
      config, "test_invoke_request_with_compressed_payload_md_override_3", 0,
      GRPC_COMPRESS_DEFLATE, GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE,
      GRPC_COMPRESS_NONE, &identity_compression_override, false,
      /*ignored*/ GRPC_COMPRESS_LEVEL_NONE, false);
}

static void test_invoke_request_with_disabled_algorithm(
    grpc_end2end_test_config config) {
  request_for_disabled_algorithm(config,
                                 "test_invoke_request_with_disabled_algorithm",
                                 0, GRPC_COMPRESS_GZIP, GRPC_COMPRESS_GZIP,
                                 GRPC_STATUS_UNIMPLEMENTED, nullptr);
}

void compressed_payload(grpc_end2end_test_config config) {
  test_invoke_request_with_exceptionally_uncompressed_payload(config);
  test_invoke_request_with_uncompressed_payload(config);
  test_invoke_request_with_compressed_payload(config);
  test_invoke_request_with_send_message_before_initial_metadata(config);
  test_invoke_request_with_server_level(config);
  test_invoke_request_with_compressed_payload_md_override(config);
  test_invoke_request_with_disabled_algorithm(config);
}

void compressed_payload_pre_init(void) {}
