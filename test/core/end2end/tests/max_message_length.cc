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

#include <stdint.h>
#include <string.h>

#include "absl/strings/match.h"

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/codegen/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char* test_name,
                                            grpc_channel_args* client_args,
                                            grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  // We intentionally do not pass the client and server args to
  // create_fixture(), since we don't want the limit enforced on the
  // proxy, only on the backend server.
  f = config.create_fixture(nullptr, nullptr);
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
  grpc_server_shutdown_and_notify(f->server, f->cq, tag(1000));
  grpc_event ev = grpc_completion_queue_next(
      f->cq, grpc_timeout_seconds_to_deadline(5), nullptr);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(ev.tag == tag(1000));
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
}

// Test with request larger than the limit.
// If send_limit is true, applies send limit on client; otherwise, applies
// recv limit on server.
static void test_max_message_length_on_request(grpc_end2end_test_config config,
                                               bool send_limit,
                                               bool use_service_config,
                                               bool use_string_json_value) {
  gpr_log(GPR_INFO,
          "testing request with send_limit=%d use_service_config=%d "
          "use_string_json_value=%d",
          send_limit, use_service_config, use_string_json_value);

  grpc_end2end_test_fixture f;
  grpc_call* c = nullptr;
  grpc_call* s = nullptr;
  grpc_op ops[6];
  grpc_op* op;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer* recv_payload = nullptr;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  grpc_channel_args* client_args = nullptr;
  grpc_channel_args* server_args = nullptr;
  if (use_service_config) {
    // We don't currently support service configs on the server side.
    GPR_ASSERT(send_limit);
    grpc_arg arg;
    arg.type = GRPC_ARG_STRING;
    arg.key = const_cast<char*>(GRPC_ARG_SERVICE_CONFIG);
    arg.value.string =
        use_string_json_value
            ? const_cast<char*>(
                  "{\n"
                  "  \"methodConfig\": [ {\n"
                  "    \"name\": [\n"
                  "      { \"service\": \"service\", \"method\": \"method\" }\n"
                  "    ],\n"
                  "    \"maxRequestMessageBytes\": \"5\"\n"
                  "  } ]\n"
                  "}")
            : const_cast<char*>(
                  "{\n"
                  "  \"methodConfig\": [ {\n"
                  "    \"name\": [\n"
                  "      { \"service\": \"service\", \"method\": \"method\" }\n"
                  "    ],\n"
                  "    \"maxRequestMessageBytes\": 5\n"
                  "  } ]\n"
                  "}");
    client_args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  } else {
    // Set limit via channel args.
    grpc_arg arg;
    arg.key = send_limit
                  ? const_cast<char*>(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH)
                  : const_cast<char*>(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH);
    arg.type = GRPC_ARG_INTEGER;
    arg.value.integer = 5;
    grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
    if (send_limit) {
      client_args = args;
    } else {
      server_args = args;
    }
  }

  f = begin_test(config, "test_max_request_message_length", client_args,
                 server_args);
  {
    grpc_core::ExecCtx exec_ctx;
    if (client_args != nullptr) grpc_channel_args_destroy(client_args);
    if (server_args != nullptr) grpc_channel_args_destroy(server_args);
  }

  grpc_core::CqVerifier cqv(f.cq);

  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/service/method"),
                               nullptr, gpr_inf_future(GPR_CLOCK_REALTIME),
                               nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
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

  if (send_limit) {
    cqv.Expect(tag(1), true);
    cqv.Verify();
    goto done;
  }

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(tag(101), true);
  cqv.Verify();

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(tag(102), true);
  cqv.Expect(tag(1), true);
  cqv.Verify();

  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  GPR_ASSERT(was_cancelled == 1);

done:
  GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
  GPR_ASSERT(
      grpc_slice_str_cmp(
          details, send_limit
                       ? "Sent message larger than max (11 vs. 5)"
                       : "Received message larger than max (11 vs. 5)") == 0);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(recv_payload);

  grpc_call_unref(c);
  if (s != nullptr) grpc_call_unref(s);

  end_test(&f);
  config.tear_down_data(&f);
}

// Test with response larger than the limit.
// If send_limit is true, applies send limit on server; otherwise, applies
// recv limit on client.
static void test_max_message_length_on_response(grpc_end2end_test_config config,
                                                bool send_limit,
                                                bool use_service_config,
                                                bool use_string_json_value) {
  gpr_log(GPR_INFO,
          "testing response with send_limit=%d use_service_config=%d "
          "use_string_json_value=%d",
          send_limit, use_service_config, use_string_json_value);

  grpc_end2end_test_fixture f;
  grpc_call* c = nullptr;
  grpc_call* s = nullptr;
  grpc_op ops[6];
  grpc_op* op;
  grpc_slice response_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer* recv_payload = nullptr;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  grpc_channel_args* client_args = nullptr;
  grpc_channel_args* server_args = nullptr;
  if (use_service_config) {
    // We don't currently support service configs on the server side.
    GPR_ASSERT(!send_limit);
    grpc_arg arg;
    arg.type = GRPC_ARG_STRING;
    arg.key = const_cast<char*>(GRPC_ARG_SERVICE_CONFIG);
    arg.value.string = const_cast<char*>(
        use_string_json_value
            ? "{\n"
              "  \"methodConfig\": [ {\n"
              "    \"name\": [\n"
              "      { \"service\": \"service\", \"method\": \"method\" }\n"
              "    ],\n"
              "    \"maxResponseMessageBytes\": \"5\"\n"
              "  } ]\n"
              "}"
            : "{\n"
              "  \"methodConfig\": [ {\n"
              "    \"name\": [\n"
              "      { \"service\": \"service\", \"method\": \"method\" }\n"
              "    ],\n"
              "    \"maxResponseMessageBytes\": 5\n"
              "  } ]\n"
              "}");
    client_args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  } else {
    // Set limit via channel args.
    grpc_arg arg;
    arg.key = send_limit
                  ? const_cast<char*>(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH)
                  : const_cast<char*>(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH);
    arg.type = GRPC_ARG_INTEGER;
    arg.value.integer = 5;
    grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
    if (send_limit) {
      server_args = args;
    } else {
      client_args = args;
    }
  }

  f = begin_test(config, "test_max_response_message_length", client_args,
                 server_args);
  {
    grpc_core::ExecCtx exec_ctx;
    if (client_args != nullptr) grpc_channel_args_destroy(client_args);
    if (server_args != nullptr) grpc_channel_args_destroy(server_args);
  }
  grpc_core::CqVerifier cqv(f.cq);

  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/service/method"),
                               nullptr, gpr_inf_future(GPR_CLOCK_REALTIME),
                               nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
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
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_payload;
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
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(tag(101), true);
  cqv.Verify();

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(tag(102), true);
  cqv.Expect(tag(1), true);
  cqv.Verify();

  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
  GPR_ASSERT(
      grpc_slice_str_cmp(
          details, send_limit
                       ? "Sent message larger than max (11 vs. 5)"
                       : "Received message larger than max (11 vs. 5)") == 0);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(recv_payload);

  grpc_call_unref(c);
  if (s != nullptr) grpc_call_unref(s);
  end_test(&f);
  config.tear_down_data(&f);
}

static grpc_metadata gzip_compression_override() {
  grpc_metadata gzip_compression_override;
  gzip_compression_override.key =
      grpc_slice_from_static_string("grpc-internal-encoding-request");
  gzip_compression_override.value = grpc_slice_from_static_string("gzip");
  memset(&gzip_compression_override.internal_data, 0,
         sizeof(gzip_compression_override.internal_data));
  return gzip_compression_override;
}

// Test receive message limit with compressed request larger than the limit
static void test_max_receive_message_length_on_compressed_request(
    grpc_end2end_test_config config, bool minimal_stack) {
  gpr_log(GPR_INFO,
          "test max receive message length on compressed request with "
          "minimal_stack=%d",
          minimal_stack);
  grpc_end2end_test_fixture f;
  grpc_call* c = nullptr;
  grpc_call* s = nullptr;
  grpc_op ops[6];
  grpc_op* op;
  grpc_slice request_payload_slice = grpc_slice_malloc(1024);
  memset(GRPC_SLICE_START_PTR(request_payload_slice), 'a', 1024);
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer* recv_payload = nullptr;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details, status_details;
  int was_cancelled = 2;

  // Set limit via channel args.
  grpc_arg arg[2];
  arg[0] = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH), 5);
  arg[1] = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_MINIMAL_STACK), minimal_stack);
  grpc_channel_args* server_args =
      grpc_channel_args_copy_and_add(nullptr, arg, 2);

  f = begin_test(config, "test_max_request_message_length", nullptr,
                 server_args);
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(server_args);
  }
  grpc_core::CqVerifier cqv(f.cq);
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/service/method"),
                               nullptr, gpr_inf_future(GPR_CLOCK_REALTIME),
                               nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  grpc_metadata compression_md = gzip_compression_override();
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 1;
  op->data.send_initial_metadata.metadata = &compression_md;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
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

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(tag(101), true);
  cqv.Verify();

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  if (minimal_stack) {
    /* Expect the RPC to proceed normally for a minimal stack */
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
    op->data.send_status_from_server.trailing_metadata_count = 0;
    op->data.send_status_from_server.status = GRPC_STATUS_OK;
    status_details = grpc_slice_from_static_string("xyz");
    op->data.send_status_from_server.status_details = &status_details;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
  }
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(tag(102), true);
  cqv.Expect(tag(1), true);
  cqv.Verify();

  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  if (minimal_stack) {
    /* We do not perform message size checks for minimal stack. */
    GPR_ASSERT(status == GRPC_STATUS_OK);
  } else {
    GPR_ASSERT(was_cancelled == 1);
    GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
    GPR_ASSERT(absl::StartsWith(grpc_core::StringViewFromSlice(details),
                                "Received message larger than max"));
  }
  grpc_slice_unref(details);
  grpc_slice_unref(request_payload_slice);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(recv_payload);
  grpc_call_unref(c);
  if (s != nullptr) grpc_call_unref(s);

  end_test(&f);
  config.tear_down_data(&f);
}

// Test receive message limit with compressed response larger than the limit.
static void test_max_receive_message_length_on_compressed_response(
    grpc_end2end_test_config config, bool minimal_stack) {
  gpr_log(GPR_INFO,
          "testing max receive message length on compressed response with "
          "minimal_stack=%d",
          minimal_stack);
  grpc_end2end_test_fixture f;
  grpc_call* c = nullptr;
  grpc_call* s = nullptr;
  grpc_op ops[6];
  grpc_op* op;
  grpc_slice response_payload_slice = grpc_slice_malloc(1024);
  memset(GRPC_SLICE_START_PTR(response_payload_slice), 'a', 1024);
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer* recv_payload = nullptr;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  // Set limit via channel args.
  grpc_arg arg[2];
  arg[0] = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH), 5);
  arg[1] = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_MINIMAL_STACK), minimal_stack);
  grpc_channel_args* client_args =
      grpc_channel_args_copy_and_add(nullptr, arg, 2);

  f = begin_test(config, "test_max_response_message_length", client_args,
                 nullptr);
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(client_args);
  }
  grpc_core::CqVerifier cqv(f.cq);

  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/service/method"),
                               nullptr, gpr_inf_future(GPR_CLOCK_REALTIME),
                               nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
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
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_payload;
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
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(tag(101), true);
  cqv.Verify();

  grpc_metadata compression_md = gzip_compression_override();
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 1;
  op->data.send_initial_metadata.metadata = &compression_md;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(tag(102), true);
  cqv.Expect(tag(1), true);
  cqv.Verify();

  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  if (minimal_stack) {
    /* We do not perform message size checks for minimal stack. */
    GPR_ASSERT(status == GRPC_STATUS_OK);
  } else {
    GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
    GPR_ASSERT(absl::StartsWith(grpc_core::StringViewFromSlice(details),
                                "Received message larger than max"));
  }
  grpc_slice_unref(details);
  grpc_slice_unref(response_payload_slice);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(recv_payload);

  grpc_call_unref(c);
  if (s != nullptr) grpc_call_unref(s);

  end_test(&f);
  config.tear_down_data(&f);
}

void max_message_length(grpc_end2end_test_config config) {
  test_max_message_length_on_request(config, false /* send_limit */,
                                     false /* use_service_config */,
                                     false /* use_string_json_value */);
  test_max_message_length_on_request(config, true /* send_limit */,
                                     false /* use_service_config */,
                                     false /* use_string_json_value */);
  test_max_message_length_on_response(config, false /* send_limit */,
                                      false /* use_service_config */,
                                      false /* use_string_json_value */);
  test_max_message_length_on_response(config, true /* send_limit */,
                                      false /* use_service_config */,
                                      false /* use_string_json_value */);
  test_max_message_length_on_request(config, true /* send_limit */,
                                     true /* use_service_config */,
                                     false /* use_string_json_value */);
  test_max_message_length_on_request(config, true /* send_limit */,
                                     true /* use_service_config */,
                                     true /* use_string_json_value */);
  test_max_message_length_on_response(config, false /* send_limit */,
                                      true /* use_service_config */,
                                      false /* use_string_json_value */);
  test_max_message_length_on_response(config, false /* send_limit */,
                                      true /* use_service_config */,
                                      true /* use_string_json_value */);
  /* The following tests are not useful for inproc transport and do not work
   * with our simple proxy. */
  if (strcmp(config.name, "inproc") != 0 &&
      (config.feature_mask & FEATURE_MASK_SUPPORTS_REQUEST_PROXYING) == 0) {
    test_max_receive_message_length_on_compressed_request(config, false);
    test_max_receive_message_length_on_compressed_request(config, true);
    test_max_receive_message_length_on_compressed_response(config, false);
    test_max_receive_message_length_on_compressed_response(config, true);
  }
}

void max_message_length_pre_init(void) {}
