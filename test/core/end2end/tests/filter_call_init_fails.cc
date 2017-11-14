/*
 *
 * Copyright 2016 gRPC authors.
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

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/surface/channel_init.h"
#include "test/core/end2end/cq_verifier.h"

enum { TIMEOUT = 200000 };

static bool g_enable_server_channel_filter = false;
static bool g_enable_client_channel_filter = false;
static bool g_enable_client_subchannel_filter = false;

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

// Simple request via a SERVER_CHANNEL filter that always fails to
// initialize the call.
static void test_server_channel_filter(grpc_end2end_test_config config) {
  grpc_call* c;
  grpc_call* s;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_end2end_test_fixture f =
      begin_test(config, "filter_call_init_fails", nullptr, nullptr);
  cq_verifier* cqv = cq_verifier_create(f.cq);
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

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/foo"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
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
  op->data.send_initial_metadata.metadata = nullptr;
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_PERMISSION_DENIED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "access denied"));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);

  cq_verifier_destroy(cqv);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(request_payload_recv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Simple request via a CLIENT_CHANNEL or CLIENT_DIRECT_CHANNEL filter
// that always fails to initialize the call.
static void test_client_channel_filter(grpc_end2end_test_config config) {
  grpc_call* c;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  gpr_timespec deadline = five_seconds_from_now();
  grpc_end2end_test_fixture f =
      begin_test(config, "filter_call_init_fails", nullptr, nullptr);
  cq_verifier* cqv = cq_verifier_create(f.cq);
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

  c = grpc_channel_create_call(
      f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/foo"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
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
  op->data.send_initial_metadata.metadata = nullptr;
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_PERMISSION_DENIED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "access denied"));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);

  cq_verifier_destroy(cqv);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(request_payload_recv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Simple request via a CLIENT_SUBCHANNEL filter that always fails to
// initialize the call.
static void test_client_subchannel_filter(grpc_end2end_test_config config) {
  grpc_call* c;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  gpr_timespec deadline = five_seconds_from_now();
  grpc_end2end_test_fixture f =
      begin_test(config, "filter_call_init_fails", nullptr, nullptr);
  cq_verifier* cqv = cq_verifier_create(f.cq);
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

  c = grpc_channel_create_call(
      f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/foo"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
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
  op->data.send_initial_metadata.metadata = nullptr;
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

  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_PERMISSION_DENIED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "access denied"));

  // Reset and create a new call.  (The first call uses a different code
  // path in client_channel.c than subsequent calls on the same channel,
  // and we need to test both.)
  grpc_call_unref(c);
  status = GRPC_STATUS_OK;
  grpc_slice_unref(details);
  details = grpc_empty_slice();

  c = grpc_channel_create_call(
      f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/foo"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      nullptr);
  GPR_ASSERT(c);

  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(2), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(2), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_PERMISSION_DENIED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "access denied"));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);

  cq_verifier_destroy(cqv);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(request_payload_recv);

  end_test(&f);
  config.tear_down_data(&f);
}

/*******************************************************************************
 * Test filter - always fails to initialize a call
 */

static grpc_error* init_call_elem(grpc_exec_ctx* exec_ctx,
                                  grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  return grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("access denied"),
      GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_PERMISSION_DENIED);
}

static void destroy_call_elem(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {}

static grpc_error* init_channel_elem(grpc_exec_ctx* exec_ctx,
                                     grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  return GRPC_ERROR_NONE;
}

static void destroy_channel_elem(grpc_exec_ctx* exec_ctx,
                                 grpc_channel_element* elem) {}

static const grpc_channel_filter test_filter = {
    grpc_call_next_op,
    grpc_channel_next_op,
    0,
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    0,
    init_channel_elem,
    destroy_channel_elem,
    grpc_channel_next_get_info,
    "filter_call_init_fails"};

/*******************************************************************************
 * Registration
 */

static bool maybe_add_server_channel_filter(grpc_exec_ctx* exec_ctx,
                                            grpc_channel_stack_builder* builder,
                                            void* arg) {
  if (g_enable_server_channel_filter) {
    // Want to add the filter as close to the end as possible, to make
    // sure that all of the filters work well together.  However, we
    // can't add it at the very end, because the connected channel filter
    // must be the last one.  So we add it right before the last one.
    grpc_channel_stack_builder_iterator* it =
        grpc_channel_stack_builder_create_iterator_at_last(builder);
    GPR_ASSERT(grpc_channel_stack_builder_move_prev(it));
    const bool retval = grpc_channel_stack_builder_add_filter_before(
        it, &test_filter, nullptr, nullptr);
    grpc_channel_stack_builder_iterator_destroy(it);
    return retval;
  } else {
    return true;
  }
}

static bool maybe_add_client_channel_filter(grpc_exec_ctx* exec_ctx,
                                            grpc_channel_stack_builder* builder,
                                            void* arg) {
  if (g_enable_client_channel_filter) {
    // Want to add the filter as close to the end as possible, to make
    // sure that all of the filters work well together.  However, we
    // can't add it at the very end, because the connected channel filter
    // must be the last one.  So we add it right before the last one.
    grpc_channel_stack_builder_iterator* it =
        grpc_channel_stack_builder_create_iterator_at_last(builder);
    GPR_ASSERT(grpc_channel_stack_builder_move_prev(it));
    const bool retval = grpc_channel_stack_builder_add_filter_before(
        it, &test_filter, nullptr, nullptr);
    grpc_channel_stack_builder_iterator_destroy(it);
    return retval;
  } else {
    return true;
  }
}

static bool maybe_add_client_subchannel_filter(
    grpc_exec_ctx* exec_ctx, grpc_channel_stack_builder* builder, void* arg) {
  if (g_enable_client_subchannel_filter) {
    // Want to add the filter as close to the end as possible, to make
    // sure that all of the filters work well together.  However, we
    // can't add it at the very end, because the client channel filter
    // must be the last one.  So we add it right before the last one.
    grpc_channel_stack_builder_iterator* it =
        grpc_channel_stack_builder_create_iterator_at_last(builder);
    GPR_ASSERT(grpc_channel_stack_builder_move_prev(it));
    const bool retval = grpc_channel_stack_builder_add_filter_before(
        it, &test_filter, nullptr, nullptr);
    grpc_channel_stack_builder_iterator_destroy(it);
    return retval;
  } else {
    return true;
  }
}

static void init_plugin(void) {
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL, INT_MAX,
                                   maybe_add_server_channel_filter, nullptr);
  grpc_channel_init_register_stage(GRPC_CLIENT_CHANNEL, INT_MAX,
                                   maybe_add_client_channel_filter, nullptr);
  grpc_channel_init_register_stage(GRPC_CLIENT_SUBCHANNEL, INT_MAX,
                                   maybe_add_client_subchannel_filter, nullptr);
  grpc_channel_init_register_stage(GRPC_CLIENT_DIRECT_CHANNEL, INT_MAX,
                                   maybe_add_client_channel_filter, nullptr);
}

static void destroy_plugin(void) {}

void filter_call_init_fails(grpc_end2end_test_config config) {
  gpr_log(GPR_INFO, "Testing SERVER_CHANNEL filter.");
  g_enable_server_channel_filter = true;
  test_server_channel_filter(config);
  g_enable_server_channel_filter = false;
  gpr_log(GPR_INFO, "Testing CLIENT_CHANNEL / CLIENT_DIRECT_CHANNEL filter.");
  g_enable_client_channel_filter = true;
  test_client_channel_filter(config);
  g_enable_client_channel_filter = false;
  if (config.feature_mask & FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL) {
    gpr_log(GPR_INFO, "Testing CLIENT_SUBCHANNEL filter.");
    g_enable_client_subchannel_filter = true;
    test_client_subchannel_filter(config);
    g_enable_client_subchannel_filter = false;
  }
}

void filter_call_init_fails_pre_init(void) {
  grpc_register_plugin(init_plugin, destroy_plugin);
}
