/*
 *
 * Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/flow_control.h"

#include <stdlib.h>
#include <string.h>

#include <functional>
#include <set>
#include <thread>

#include <gmock/gmock.h>

#include <grpc/grpc.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace {

class TransportTargetWindowSizeMocker
    : public grpc_core::chttp2::TestOnlyTransportTargetWindowEstimatesMocker {
 public:
  static constexpr uint32_t kLargeInitialWindowSize = 1u << 31;
  static constexpr uint32_t kSmallInitialWindowSize = 0;

  double ComputeNextTargetInitialWindowSizeFromPeriodicUpdate(
      double /* current_target */) override {
    if (alternating_initial_window_sizes_) {
      window_size_ = (window_size_ == kLargeInitialWindowSize)
                         ? kSmallInitialWindowSize
                         : kLargeInitialWindowSize;
    }
    return window_size_;
  }

  // Alternates the initial window size targets. Computes a low values if it was
  // previously high, or a high value if it was previously low.
  void AlternateTargetInitialWindowSizes() {
    alternating_initial_window_sizes_ = true;
  }

  void Reset() {
    alternating_initial_window_sizes_ = false;
    window_size_ = kLargeInitialWindowSize;
  }

 private:
  bool alternating_initial_window_sizes_ = false;
  double window_size_ = kLargeInitialWindowSize;
};

TransportTargetWindowSizeMocker* g_target_initial_window_size_mocker;

void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

void VerifyChannelReady(grpc_channel* channel, grpc_completion_queue* cq) {
  grpc_connectivity_state state =
      grpc_channel_check_connectivity_state(channel, 1 /* try_to_connect */);
  while (state != GRPC_CHANNEL_READY) {
    grpc_channel_watch_connectivity_state(
        channel, state, grpc_timeout_seconds_to_deadline(5), cq, nullptr);
    grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(5),
                               nullptr);
    state = grpc_channel_check_connectivity_state(channel, 0);
  }
}

void VerifyChannelConnected(grpc_channel* channel, grpc_completion_queue* cq) {
  // Verify channel is connected. Use a ping to make sure that clients
  // tries sending/receiving bytes if the channel is connected.
  grpc_channel_ping(channel, cq, reinterpret_cast<void*>(2000), nullptr);
  grpc_event ev = grpc_completion_queue_next(
      cq, grpc_timeout_seconds_to_deadline(5), nullptr);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(ev.tag == reinterpret_cast<void*>(2000));
  GPR_ASSERT(ev.success == 1);
  GPR_ASSERT(grpc_channel_check_connectivity_state(channel, 0) ==
             GRPC_CHANNEL_READY);
}

// Shuts down and destroys the server.
void ServerShutdownAndDestroy(grpc_server* server, grpc_completion_queue* cq) {
  // Shutdown and destroy server
  grpc_server_shutdown_and_notify(server, cq, reinterpret_cast<void*>(1000));
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .tag != reinterpret_cast<void*>(1000)) {
  }
  grpc_server_destroy(server);
}

grpc_slice LargeSlice(void) {
  grpc_slice slice = grpc_slice_malloc(10000000);  // ~10MB
  memset(GRPC_SLICE_START_PTR(slice), 'x', GRPC_SLICE_LENGTH(slice));
  return slice;
}

void PerformCallWithLargePayload(grpc_channel* channel, grpc_server* server,
                                 grpc_completion_queue* cq) {
  grpc_slice request_payload_slice = LargeSlice();
  grpc_slice response_payload_slice = LargeSlice();
  grpc_call* c;
  grpc_call* s;
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  cq_verifier* cqv = cq_verifier_create(cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* request_payload_recv = nullptr;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(30);
  c = grpc_channel_create_call(channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
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
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
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

  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101));
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
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
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
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(103),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(103), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
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

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(request_payload_recv);
  grpc_byte_buffer_destroy(response_payload_recv);
  grpc_slice_unref(request_payload_slice);
  grpc_slice_unref(response_payload_slice);
}

class FlowControlTest : public ::testing::Test {
 protected:
  void SetUp() override {
    cq_ = grpc_completion_queue_create_for_next(nullptr);
    // create the server
    std::string server_address =
        grpc_core::JoinHostPort("localhost", grpc_pick_unused_port_or_die());
    grpc_arg server_args[] = {
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_HTTP2_MAX_PING_STRIKES), 0),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH), -1),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH), -1)};
    grpc_channel_args server_channel_args = {GPR_ARRAY_SIZE(server_args),
                                             server_args};
    server_ = grpc_server_create(&server_channel_args, nullptr);
    grpc_server_register_completion_queue(server_, cq_, nullptr);
    GPR_ASSERT(
        grpc_server_add_insecure_http2_port(server_, server_address.c_str()));
    grpc_server_start(server_);
    // create the channel (bdp pings are enabled by default)
    grpc_arg client_args[] = {
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA), 0),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS), 1),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH), -1),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH), -1)};
    grpc_channel_args client_channel_args = {GPR_ARRAY_SIZE(client_args),
                                             client_args};
    channel_ = grpc_insecure_channel_create(server_address.c_str(),
                                            &client_channel_args, nullptr);
    VerifyChannelReady(channel_, cq_);
    g_target_initial_window_size_mocker->Reset();
  }

  void TearDown() override {
    // shutdown and destroy the client and server
    grpc_channel_destroy(channel_);
    ServerShutdownAndDestroy(server_, cq_);
    grpc_completion_queue_shutdown(cq_);
    while (grpc_completion_queue_next(cq_, gpr_inf_future(GPR_CLOCK_REALTIME),
                                      nullptr)
               .type != GRPC_QUEUE_SHUTDOWN) {
    }
    grpc_completion_queue_destroy(cq_);
  }

  grpc_server* server_ = nullptr;
  grpc_channel* channel_ = nullptr;
  grpc_completion_queue* cq_ = nullptr;
};

TEST_F(FlowControlTest,
       TestLargeWindowSizeUpdatesDoNotCauseIllegalFlowControlWindows) {
  for (int i = 0; i < 10; ++i) {
    PerformCallWithLargePayload(channel_, server_, cq_);
    VerifyChannelConnected(channel_, cq_);
  }
}

TEST_F(FlowControlTest, TestWindowSizeUpdatesDoNotCauseStalledStreams) {
  g_target_initial_window_size_mocker->AlternateTargetInitialWindowSizes();
  for (int i = 0; i < 100; ++i) {
    PerformCallWithLargePayload(channel_, server_, cq_);
    VerifyChannelConnected(channel_, cq_);
  }
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Make sure that we will have an active poller on all client-side fd's that
  // are capable of sending and receiving even in the case that we don't have an
  // active RPC operation on the fd.
  GPR_GLOBAL_CONFIG_SET(grpc_client_channel_backup_poll_interval_ms, 1);
  ::grpc_core::chttp2::g_test_only_transport_flow_control_window_check = true;
  g_target_initial_window_size_mocker = new TransportTargetWindowSizeMocker();
  grpc_core::chttp2::g_test_only_transport_target_window_estimates_mocker =
      g_target_initial_window_size_mocker;
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
