//
//
// Copyright 2020 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include <string.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "absl/types/optional.h"

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/sync.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace {

class TransportTargetWindowEstimatesMocker
    : public grpc_core::chttp2::TestOnlyTransportTargetWindowEstimatesMocker {
 public:
  explicit TransportTargetWindowEstimatesMocker() {}

  double ComputeNextTargetInitialWindowSizeFromPeriodicUpdate(
      double current_target) override {
    const double kTinyWindow = 512;
    const double kSmallWindow = 8192;
    // The goal is to bounce back and forth between 512 and 8192 initial window
    // sizes, in order to get the following to happen at the server (in order):
    //
    // 1) Stall the server-side RPC's outgoing message on stream window flow
    // control.
    //
    // 2) Send another settings frame with a change in initial window
    // size setting, which will make the server-side call go writable.
    if (current_target > kTinyWindow) {
      return kTinyWindow;
    } else {
      return kSmallWindow;
    }
  }
};

void StartCall(grpc_call* call, grpc_completion_queue* cq) {
  grpc_op ops[1];
  grpc_op* op;
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op->reserved = nullptr;
  op++;
  void* tag = call;
  grpc_call_error error = grpc_call_start_batch(
      call, ops, static_cast<size_t>(op - ops), tag, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  grpc_event event = grpc_completion_queue_next(
      cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(event.success);
  GPR_ASSERT(event.tag == tag);
}

void FinishCall(grpc_call* call, grpc_completion_queue* cq) {
  grpc_op ops[4];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_status_code status;
  grpc_slice details;
  grpc_byte_buffer* recv_payload = nullptr;
  memset(ops, 0, sizeof(ops));
  op = ops;
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
  void* tag = call;
  grpc_call_error error = grpc_call_start_batch(
      call, ops, static_cast<size_t>(op - ops), tag, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  grpc_event event = grpc_completion_queue_next(
      cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(event.success);
  GPR_ASSERT(event.tag == tag);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_byte_buffer_destroy(recv_payload);
  grpc_slice_unref(details);
}

class TestServer {
 public:
  explicit TestServer() {
    cq_ = grpc_completion_queue_create_for_next(nullptr);
    server_ = grpc_server_create(nullptr, nullptr);
    address_ = grpc_core::JoinHostPort("[::1]", grpc_pick_unused_port_or_die());
    grpc_server_register_completion_queue(server_, cq_, nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    GPR_ASSERT(
        grpc_server_add_http2_port(server_, address_.c_str(), server_creds));
    grpc_server_credentials_release(server_creds);
    grpc_server_start(server_);
    accept_thread_ = std::thread(std::bind(&TestServer::AcceptThread, this));
  }

  int ShutdownAndGetNumCallsHandled() {
    {
      // prevent the server from requesting any more calls
      grpc_core::MutexLock lock(&shutdown_mu_);
      shutdown_ = true;
    }
    grpc_server_shutdown_and_notify(server_, cq_, this /* tag */);
    accept_thread_.join();
    grpc_server_destroy(server_);
    grpc_completion_queue_shutdown(cq_);
    while (grpc_completion_queue_next(cq_, gpr_inf_future(GPR_CLOCK_REALTIME),
                                      nullptr)
               .type != GRPC_QUEUE_SHUTDOWN) {
    }
    grpc_completion_queue_destroy(cq_);
    return num_calls_handled_;
  }

  std::string address() const { return address_; }

 private:
  void AcceptThread() {
    std::vector<std::thread> rpc_threads;
    bool got_shutdown_and_notify_tag = false;
    while (!got_shutdown_and_notify_tag) {
      void* request_call_tag = &rpc_threads;
      grpc_call_details call_details;
      grpc_call_details_init(&call_details);
      grpc_call* call = nullptr;
      grpc_completion_queue* call_cq = nullptr;
      grpc_metadata_array request_metadata_recv;
      grpc_metadata_array_init(&request_metadata_recv);
      {
        grpc_core::MutexLock lock(&shutdown_mu_);
        if (!shutdown_) {
          call_cq = grpc_completion_queue_create_for_next(nullptr);
          grpc_call_error error = grpc_server_request_call(
              server_, &call, &call_details, &request_metadata_recv, call_cq,
              cq_, request_call_tag);
          GPR_ASSERT(error == GRPC_CALL_OK);
        }
      }
      grpc_event event = grpc_completion_queue_next(
          cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
      GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
      grpc_call_details_destroy(&call_details);
      grpc_metadata_array_destroy(&request_metadata_recv);
      if (event.success) {
        if (event.tag == request_call_tag) {
          // HandleOneRpc takes ownership of its parameters
          num_calls_handled_++;
          rpc_threads.push_back(
              std::thread(std::bind(&TestServer::HandleOneRpc, call, call_cq)));
        } else if (event.tag == this /* shutdown_and_notify tag */) {
          grpc_core::MutexLock lock(&shutdown_mu_);
          GPR_ASSERT(shutdown_);
          GPR_ASSERT(call_cq == nullptr);
          got_shutdown_and_notify_tag = true;
        } else {
          GPR_ASSERT(0);
        }
      } else {
        grpc_core::MutexLock lock(&shutdown_mu_);
        GPR_ASSERT(shutdown_);
        grpc_completion_queue_destroy(call_cq);
      }
    }
    gpr_log(GPR_INFO, "test server shutdown, joining RPC threads...");
    for (auto& t : rpc_threads) {
      t.join();
    }
    gpr_log(GPR_INFO, "test server threads all finished!");
  }

  static void HandleOneRpc(grpc_call* call, grpc_completion_queue* call_cq) {
    // Send a large enough payload to get us stalled on outgoing flow control
    std::string send_payload(4 * 1024 * 1024, 'a');
    grpc_slice request_payload_slice =
        grpc_slice_from_copied_string(send_payload.c_str());
    grpc_byte_buffer* request_payload =
        grpc_raw_byte_buffer_create(&request_payload_slice, 1);
    void* tag = call_cq;
    grpc_op ops[2];
    grpc_op* op;
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->reserved = nullptr;
    op++;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = request_payload;
    op->reserved = nullptr;
    op++;
    grpc_call_error error = grpc_call_start_batch(
        call, ops, static_cast<size_t>(op - ops), tag, nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);
    std::thread poller([call_cq]() {
      // poll the connection so that we actively pick up bytes off the wire,
      // including settings frames with window size increases
      while (grpc_completion_queue_next(
                 call_cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr)
                 .type != GRPC_QUEUE_SHUTDOWN) {
      }
    });
    grpc_call_cancel(call, nullptr);
    grpc_call_unref(call);
    grpc_completion_queue_shutdown(call_cq);
    poller.join();
    grpc_completion_queue_destroy(call_cq);
    grpc_byte_buffer_destroy(request_payload);
    grpc_slice_unref(request_payload_slice);
  }

  grpc_server* server_;
  grpc_completion_queue* cq_;
  std::string address_;
  std::thread accept_thread_;
  int num_calls_handled_ = 0;
  grpc_core::Mutex shutdown_mu_;
  bool shutdown_ = false;
};

// Perform a simple RPC where the server cancels the request with
// grpc_call_cancel_with_status
TEST(Pollers, TestDontCrashWhenTryingToReproIssueFixedBy23984) {
  // 64 threads is arbitrary but chosen because, experimentally it's enough to
  // repro the targetted crash crash (which is then fixed by
  // https://github.com/grpc/grpc/pull/23984) at a very high rate.
  const int kNumCalls = 64;
  std::vector<std::thread> threads;
  threads.reserve(kNumCalls);
  std::unique_ptr<TestServer> test_server = std::make_unique<TestServer>();
  const std::string server_address = test_server->address();
  for (int i = 0; i < kNumCalls; i++) {
    threads.push_back(std::thread([server_address]() {
      std::vector<grpc_arg> args;
      // this test is meant to create one connection to the server for each
      // of these threads
      args.push_back(grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL), true));
      grpc_channel_args* channel_args =
          grpc_channel_args_copy_and_add(nullptr, args.data(), args.size());
      grpc_channel_credentials* creds = grpc_insecure_credentials_create();
      grpc_channel* channel = grpc_channel_create(
          std::string("ipv6:" + server_address).c_str(), creds, channel_args);
      grpc_channel_credentials_release(creds);
      grpc_channel_args_destroy(channel_args);
      grpc_completion_queue* cq =
          grpc_completion_queue_create_for_next(nullptr);
      grpc_call* call = grpc_channel_create_call(
          channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
          grpc_slice_from_static_string("/foo"), nullptr,
          gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
      StartCall(call, cq);
      // Explicitly avoid reading on this RPC for a period of time. The
      // goal is to get the server side RPC to stall on it's outgoing stream
      // flow control window, as the first step in trying to trigger a bug.
      gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                   gpr_time_from_seconds(1, GPR_TIMESPAN)));
      // Note that this test doesn't really care what the status of the RPC was,
      // because we're just trying to make sure that we don't crash.
      FinishCall(call, cq);
      grpc_call_unref(call);
      grpc_channel_destroy(channel);
      grpc_completion_queue_shutdown(cq);
      while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                        nullptr)
                 .type != GRPC_QUEUE_SHUTDOWN) {
      }
      grpc_completion_queue_destroy(cq);
    }));
  }
  for (auto& thread : threads) {
    thread.join();
  }
  gpr_log(GPR_DEBUG, "All RPCs completed!");
  int num_calls_seen_at_server = test_server->ShutdownAndGetNumCallsHandled();
  if (num_calls_seen_at_server != kNumCalls) {
    gpr_log(GPR_ERROR,
            "Expected server to handle %d calls, but instead it only handled "
            "%d. This suggests some or all RPCs didn't make it to the server, "
            "which means "
            "that this test likely isn't doing what it's meant to be doing.",
            kNumCalls, num_calls_seen_at_server);
    GPR_ASSERT(0);
  }
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Make sure that we will have an active poller on all client-side fd's that
  // are capable of sending settings frames with window updates etc., even in
  // the case that we don't have an active RPC operation on the fd.
  grpc_core::ConfigVars::Overrides overrides;
  overrides.client_channel_backup_poll_interval_ms = 1;
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_core::chttp2::g_test_only_transport_target_window_estimates_mocker =
      new TransportTargetWindowEstimatesMocker();
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
