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

#include "absl/synchronization/notification.h"

#include <gmock/gmock.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#include <set>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/frame_settings.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"

#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/core/util/test_tcp_server.h"

namespace grpc_core {
namespace test {
namespace {

void* Tag(int i) { return (void*)static_cast<intptr_t>(i); }

class ClientSettingsTimeout : public ::testing::Test {
 protected:
  ClientSettingsTimeout() {
    grpc_core::ExecCtx exec_ctx;
    cq_ = grpc_completion_queue_create_for_next(nullptr);
    // create the server
    test_tcp_server_init(&test_server_, OnConnect, this);
    int server_port = grpc_pick_unused_port_or_die();
    test_tcp_server_start(&test_server_, server_port);
    test_tcp_server_poll(&test_server_, 100);
    // create the channel
    std::string server_address =
        grpc_core::JoinHostPort("localhost", server_port);
    thread_.reset(new std::thread([this]() {
      while (!notification_.HasBeenNotified()) {
        test_tcp_server_poll(&test_server_, 100);
      }
    }));
    grpc_arg connect_arg = grpc_channel_arg_integer_create(
        const_cast<char*>("grpc.testing.fixed_reconnect_backoff_ms"), 1000);
    grpc_channel_args args = {1, &connect_arg};
    channel_ =
        grpc_insecure_channel_create(server_address.c_str(), &args, nullptr);
    connected_.store(false);
  }

  ~ClientSettingsTimeout() {
    // shutdown and destroy the client and server
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_destroy(channel_);
    grpc_completion_queue_shutdown(cq_);
    notification_.Notify();
    thread_->join();
    while (grpc_completion_queue_next(cq_, gpr_inf_future(GPR_CLOCK_REALTIME),
                                      nullptr)
               .type != GRPC_QUEUE_SHUTDOWN)
      ;
    notification_.Notify();
    thread_->join();
    EXPECT_EQ(connected_, true);
    test_tcp_server_destroy(&test_server_);
    grpc_core::ExecCtx::Get()->Flush();
    grpc_completion_queue_destroy(cq_);
  }

  static void OnConnect(void* arg, grpc_endpoint* /* tcp */,
                        grpc_pollset* /*accepting_pollset*/,
                        grpc_tcp_server_acceptor* /*acceptor*/) {
    auto self = static_cast<ClientSettingsTimeout*>(arg);
    self->connected_.store(true);
  }

  std::unique_ptr<std::thread> thread_;
  absl::Notification notification_;
  test_tcp_server test_server_;
  grpc_completion_queue* cq_ = nullptr;
  grpc_server* server_ = nullptr;
  grpc_channel* channel_ = nullptr;
  std::atomic<bool> connected_;
};

TEST_F(ClientSettingsTimeout, Basic) {
  grpc_call* c;
  cq_verifier* cqv = cq_verifier_create(cq_);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  // Start a call
  c = grpc_channel_create_call(channel_, nullptr, GRPC_PROPAGATE_DEFAULTS, cq_,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);
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
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), Tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, Tag(1), 1);
  cq_verify(cqv);
  // Should fail with unavailable instead of deadline exceeded since the server
  // did not reply with settings frame.
  EXPECT_EQ(status, GRPC_STATUS_UNAVAILABLE);
  // cleanup
  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(c);
  cq_verifier_destroy(cqv);
}

}  // namespace
}  // namespace test
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
