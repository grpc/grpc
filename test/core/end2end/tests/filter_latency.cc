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

#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/surface/channel_init.h"
#include "test/core/end2end/cq_verifier.h"

enum { TIMEOUT = 200000 };

static bool g_enable_filter = false;
static gpr_mu g_mu;
static gpr_timespec g_client_latency;
static gpr_timespec g_server_latency;

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

// Simple request via a server filter that saves the reported latency value.
static void test_request(grpc_end2end_test_config config) {
  grpc_call* c;
  grpc_call* s;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_end2end_test_fixture f =
      begin_test(config, "filter_latency", nullptr, nullptr);
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
  int was_cancelled = 2;

  gpr_mu_lock(&g_mu);
  g_client_latency = gpr_time_0(GPR_TIMESPAN);
  g_server_latency = gpr_time_0(GPR_TIMESPAN);
  gpr_mu_unlock(&g_mu);
  const gpr_timespec start_time = gpr_now(GPR_CLOCK_MONOTONIC);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
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
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_string = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_string;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(s);
  grpc_call_unref(c);

  cq_verifier_destroy(cqv);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(request_payload_recv);

  end_test(&f);
  config.tear_down_data(&f);

  const gpr_timespec end_time = gpr_now(GPR_CLOCK_MONOTONIC);
  const gpr_timespec max_latency = gpr_time_sub(end_time, start_time);

  // Perform checks after test tear-down
  // Guards against the case that there's outstanding channel-related work on a
  // call prior to verification
  gpr_mu_lock(&g_mu);
  GPR_ASSERT(gpr_time_cmp(max_latency, g_client_latency) >= 0);
  GPR_ASSERT(gpr_time_cmp(gpr_time_0(GPR_TIMESPAN), g_client_latency) <= 0);
  GPR_ASSERT(gpr_time_cmp(max_latency, g_server_latency) >= 0);
  GPR_ASSERT(gpr_time_cmp(gpr_time_0(GPR_TIMESPAN), g_server_latency) <= 0);
  // Server latency should always be smaller than client latency, however since
  // we only calculate latency at destruction time, and that might mean that we
  // need to wait for outstanding channel-related work, this isn't verifiable
  // right now (the server MAY hold on to the call for longer than the client).
  // GPR_ASSERT(gpr_time_cmp(g_server_latency, g_client_latency) < 0);
  gpr_mu_unlock(&g_mu);
}

/*******************************************************************************
 * Test latency filter
 */

static grpc_error* init_call_elem(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  return GRPC_ERROR_NONE;
}

static void client_destroy_call_elem(grpc_call_element* elem,
                                     const grpc_call_final_info* final_info,
                                     grpc_closure* ignored) {
  gpr_mu_lock(&g_mu);
  g_client_latency = final_info->stats.latency;
  gpr_mu_unlock(&g_mu);
}

static void server_destroy_call_elem(grpc_call_element* elem,
                                     const grpc_call_final_info* final_info,
                                     grpc_closure* ignored) {
  gpr_mu_lock(&g_mu);
  g_server_latency = final_info->stats.latency;
  gpr_mu_unlock(&g_mu);
}

static grpc_error* init_channel_elem(grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  return GRPC_ERROR_NONE;
}

static void destroy_channel_elem(grpc_channel_element* elem) {}

static const grpc_channel_filter test_client_filter = {
    grpc_call_next_op,
    grpc_channel_next_op,
    0,
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    client_destroy_call_elem,
    0,
    init_channel_elem,
    destroy_channel_elem,
    grpc_channel_next_get_info,
    "client_filter_latency"};

static const grpc_channel_filter test_server_filter = {
    grpc_call_next_op,
    grpc_channel_next_op,
    0,
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    server_destroy_call_elem,
    0,
    init_channel_elem,
    destroy_channel_elem,
    grpc_channel_next_get_info,
    "server_filter_latency"};

/*******************************************************************************
 * Registration
 */

static bool maybe_add_filter(grpc_channel_stack_builder* builder, void* arg) {
  grpc_channel_filter* filter = static_cast<grpc_channel_filter*>(arg);
  if (g_enable_filter) {
    // Want to add the filter as close to the end as possible, to make
    // sure that all of the filters work well together.  However, we
    // can't add it at the very end, because the connected channel filter
    // must be the last one.  So we add it right before the last one.
    grpc_channel_stack_builder_iterator* it =
        grpc_channel_stack_builder_create_iterator_at_last(builder);
    const bool retval = grpc_channel_stack_builder_add_filter_before(
        it, filter, nullptr, nullptr);
    grpc_channel_stack_builder_iterator_destroy(it);
    return retval;
  } else {
    return true;
  }
}

static void init_plugin(void) {
  gpr_mu_init(&g_mu);
  grpc_channel_init_register_stage(
      GRPC_CLIENT_CHANNEL, GRPC_CHANNEL_INIT_PRIORITY_MAX, maybe_add_filter,
      (void*)&test_client_filter);
  grpc_channel_init_register_stage(
      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_CHANNEL_INIT_PRIORITY_MAX,
      maybe_add_filter, (void*)&test_client_filter);
  grpc_channel_init_register_stage(
      GRPC_SERVER_CHANNEL, GRPC_CHANNEL_INIT_PRIORITY_MAX, maybe_add_filter,
      (void*)&test_server_filter);
}

static void destroy_plugin(void) { gpr_mu_destroy(&g_mu); }

void filter_latency(grpc_end2end_test_config config) {
  g_enable_filter = true;
  test_request(config);
  g_enable_filter = false;
}

void filter_latency_pre_init(void) {
  grpc_register_plugin(init_plugin, destroy_plugin);
}
