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

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "absl/status/status.h"

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/codegen/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

enum { TIMEOUT = 200000 };

static gpr_mu g_mu;
static gpr_timespec g_client_latency;
static gpr_timespec g_server_latency;

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

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
  grpc_server_shutdown_and_notify(f->server, f->cq, tag(1000));
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(f->cq, grpc_timeout_seconds_to_deadline(5),
                                    nullptr);
  } while (ev.type != GRPC_OP_COMPLETE || ev.tag != tag(1000));
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
  grpc_core::CqVerifier cqv(f.cq);
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
  const gpr_timespec start_time = gpr_now(GPR_CLOCK_REALTIME);

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

  cqv.Expect(tag(101), true);
  cqv.Verify();

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

  cqv.Expect(tag(102), true);
  cqv.Expect(tag(1), true);
  cqv.Verify();

  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(s);
  grpc_call_unref(c);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(request_payload_recv);

  end_test(&f);
  config.tear_down_data(&f);

  const gpr_timespec end_time = gpr_now(GPR_CLOCK_REALTIME);
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

static grpc_error_handle init_call_elem(
    grpc_call_element* /*elem*/, const grpc_call_element_args* /*args*/) {
  return absl::OkStatus();
}

static void client_destroy_call_elem(grpc_call_element* /*elem*/,
                                     const grpc_call_final_info* final_info,
                                     grpc_closure* /*ignored*/) {
  gpr_mu_lock(&g_mu);
  g_client_latency = final_info->stats.latency;
  gpr_mu_unlock(&g_mu);
}

static void server_destroy_call_elem(grpc_call_element* /*elem*/,
                                     const grpc_call_final_info* final_info,
                                     grpc_closure* /*ignored*/) {
  gpr_mu_lock(&g_mu);
  g_server_latency = final_info->stats.latency;
  gpr_mu_unlock(&g_mu);
}

static grpc_error_handle init_channel_elem(
    grpc_channel_element* /*elem*/, grpc_channel_element_args* /*args*/) {
  return absl::OkStatus();
}

static void destroy_channel_elem(grpc_channel_element* /*elem*/) {}

static const grpc_channel_filter test_client_filter = {
    grpc_call_next_op,        nullptr,
    grpc_channel_next_op,     0,
    init_call_elem,           grpc_call_stack_ignore_set_pollset_or_pollset_set,
    client_destroy_call_elem, 0,
    init_channel_elem,        grpc_channel_stack_no_post_init,
    destroy_channel_elem,     grpc_channel_next_get_info,
    "client_filter_latency"};

static const grpc_channel_filter test_server_filter = {
    grpc_call_next_op,        nullptr,
    grpc_channel_next_op,     0,
    init_call_elem,           grpc_call_stack_ignore_set_pollset_or_pollset_set,
    server_destroy_call_elem, 0,
    init_channel_elem,        grpc_channel_stack_no_post_init,
    destroy_channel_elem,     grpc_channel_next_get_info,
    "server_filter_latency"};

/*******************************************************************************
 * Registration
 */

void filter_latency(grpc_end2end_test_config config) {
  grpc_core::CoreConfiguration::RunWithSpecialConfiguration(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        grpc_core::BuildCoreConfiguration(builder);
        auto register_stage = [builder](grpc_channel_stack_type type,
                                        const grpc_channel_filter* filter) {
          builder->channel_init()->RegisterStage(
              type, INT_MAX, [filter](grpc_core::ChannelStackBuilder* builder) {
                // Want to add the filter as close to the end as possible, to
                // make sure that all of the filters work well together.
                // However, we can't add it at the very end, because the
                // connected channel filter must be the last one.  So we add it
                // right before the last one.
                auto it = builder->mutable_stack()->end();
                --it;
                builder->mutable_stack()->insert(it, filter);
                return true;
              });
        };
        register_stage(GRPC_CLIENT_CHANNEL, &test_client_filter);
        register_stage(GRPC_CLIENT_DIRECT_CHANNEL, &test_client_filter);
        register_stage(GRPC_SERVER_CHANNEL, &test_server_filter);
      },
      [config] { test_request(config); });
}

void filter_latency_pre_init(void) { gpr_mu_init(&g_mu); }
