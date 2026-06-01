//
// Copyright 2026 gRPC authors.
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

#include "src/core/transport/session_endpoint.h"

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>

#include "src/core/ext/transport/inproc/inproc_transport.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/notification.h"
#include "src/core/util/thd.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace {

class SessionEndpointTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_init();
    server_ = grpc_server_create(nullptr, nullptr);
    cq_ = grpc_completion_queue_create_for_next(nullptr);
    grpc_server_register_completion_queue(server_, cq_, nullptr);
    grpc_server_start(server_);

    channel_ = grpc_inproc_channel_create(server_, nullptr, nullptr);
    ASSERT_NE(channel_, nullptr);

    // Establish calls
    EstablishCalls();
  }

  void TearDown() override {
    if (client_call_ != nullptr) grpc_call_unref(client_call_);
    if (server_call_ != nullptr) grpc_call_unref(server_call_);
    grpc_channel_destroy(channel_);
    grpc_server_shutdown_and_notify(server_, cq_,
                                    reinterpret_cast<void*>(1000));
    CqVerifier cqv(cq_);
    cqv.Expect(reinterpret_cast<void*>(1000), true);
    cqv.Verify();
    grpc_server_destroy(server_);
    grpc_completion_queue_destroy(cq_);
    grpc_shutdown();
  }

  void EstablishCalls() {
    if (client_call_ != nullptr) {
      grpc_call_unref(client_call_);
    }
    if (server_call_ != nullptr) {
      grpc_call_unref(server_call_);
    }
    CqVerifier cqv(cq_);
    grpc_call* c;
    grpc_call* s;
    grpc_call_details call_details;
    grpc_metadata_array request_metadata_recv;

    grpc_call_details_init(&call_details);
    grpc_metadata_array_init(&request_metadata_recv);

    gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
    c = grpc_channel_create_call(channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
                                 cq_,
                                 grpc_slice_from_static_string("/test/method"),
                                 nullptr, deadline, nullptr);
    ASSERT_NE(c, nullptr);

    grpc_call_error error = grpc_server_request_call(
        server_, &s, &call_details, &request_metadata_recv, cq_, cq_,
        reinterpret_cast<void*>(101));
    ASSERT_EQ(error, GRPC_CALL_OK);

    // Start client initial metadata to trigger server receiving the call
    grpc_op ops[1];
    memset(ops, 0, sizeof(ops));
    ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
    ops[0].data.send_initial_metadata.count = 0;
    ops[0].flags = 0;
    ops[0].reserved = nullptr;
    error =
        grpc_call_start_batch(c, ops, 1, reinterpret_cast<void*>(102), nullptr);
    ASSERT_EQ(error, GRPC_CALL_OK);

    cqv.Expect(reinterpret_cast<void*>(101), true);
    cqv.Expect(reinterpret_cast<void*>(102), true);
    cqv.Verify();

    // Send server initial metadata
    grpc_op server_ops[1];
    memset(server_ops, 0, sizeof(server_ops));
    server_ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
    server_ops[0].data.send_initial_metadata.count = 0;
    server_ops[0].flags = 0;
    server_ops[0].reserved = nullptr;
    error = grpc_call_start_batch(s, server_ops, 1,
                                  reinterpret_cast<void*>(103), nullptr);
    ASSERT_EQ(error, GRPC_CALL_OK);

    cqv.Expect(reinterpret_cast<void*>(103), true);
    cqv.Verify();

    // Client receives initial metadata
    grpc_op client_recv_ops[1];
    memset(client_recv_ops, 0, sizeof(client_recv_ops));
    grpc_metadata_array initial_metadata_recv;
    grpc_metadata_array_init(&initial_metadata_recv);
    client_recv_ops[0].op = GRPC_OP_RECV_INITIAL_METADATA;
    client_recv_ops[0].data.recv_initial_metadata.recv_initial_metadata =
        &initial_metadata_recv;
    client_recv_ops[0].flags = 0;
    client_recv_ops[0].reserved = nullptr;
    error = grpc_call_start_batch(c, client_recv_ops, 1,
                                  reinterpret_cast<void*>(104), nullptr);
    ASSERT_EQ(error, GRPC_CALL_OK);

    cqv.Expect(reinterpret_cast<void*>(104), true);
    cqv.Verify();

    grpc_metadata_array_destroy(&initial_metadata_recv);

    client_call_ = c;
    server_call_ = s;

    grpc_metadata_array_destroy(&request_metadata_recv);
    grpc_call_details_destroy(&call_details);
  }

  std::unique_ptr<SessionEndpoint> CreateClientEndpoint() {
    return std::make_unique<SessionEndpoint>(client_call_, /*is_client=*/true);
  }

  std::unique_ptr<SessionEndpoint> CreateServerEndpoint() {
    return std::make_unique<SessionEndpoint>(server_call_, /*is_client=*/false);
  }

  grpc_server* server_ = nullptr;
  grpc_channel* channel_ = nullptr;
  grpc_completion_queue* cq_ = nullptr;
  grpc_call* client_call_ = nullptr;
  grpc_call* server_call_ = nullptr;
};

TEST_F(SessionEndpointTest, BasicBiDiDataTransfer) {
  ExecCtx exec_ctx;
  auto client_ep = CreateClientEndpoint();
  auto server_ep = CreateServerEndpoint();

  grpc_event_engine::experimental::SliceBuffer send_buffer;
  send_buffer.Append(
      grpc_event_engine::experimental::Slice::FromCopiedString("client"));

  bool client_write_done = false;
  bool server_read_done = false;
  grpc_event_engine::experimental::SliceBuffer recv_buffer;

  client_ep->Write(
      [&client_write_done](absl::Status status) {
        EXPECT_TRUE(status.ok());
        client_write_done = true;
      },
      &send_buffer, SessionEndpoint::WriteArgs{});

  server_ep->Read(
      [&server_read_done](absl::Status status) {
        EXPECT_TRUE(status.ok());
        server_read_done = true;
      },
      &recv_buffer, SessionEndpoint::ReadArgs{});

  // Poll until done
  int iterations = 0;
  while ((!client_write_done || !server_read_done) && iterations < 100) {
    grpc_completion_queue_next(cq_, grpc_timeout_milliseconds_to_deadline(10),
                               nullptr);
    exec_ctx.Flush();
    iterations++;
  }

  EXPECT_TRUE(client_write_done);
  EXPECT_TRUE(server_read_done);

  if (server_read_done) {
    EXPECT_EQ(recv_buffer.Count(), 1);
    EXPECT_EQ(recv_buffer.TakeFirst().as_string_view(), "client");
  }
}

TEST_F(SessionEndpointTest, ReadVsDestroyRace) {
  for (int i = 0; i < 1000; ++i) {
    ExecCtx exec_ctx;
    EstablishCalls();

    std::shared_ptr<SessionEndpoint> client_ep = CreateClientEndpoint();
    auto server_ep = CreateServerEndpoint();

    grpc_event_engine::experimental::SliceBuffer recv_buffer;
    std::atomic<bool> read_done{false};
    Notification read_started;

    Thread read_thread("read_thread", [client_ep, &read_done, &read_started,
                                       &recv_buffer]() mutable {
      ExecCtx exec_ctx;
      client_ep->Read(
          [&read_done](absl::Status status) {
            read_done.store(true, std::memory_order_release);
          },
          &recv_buffer, SessionEndpoint::ReadArgs{});
      read_started.Notify();
      client_ep.reset();  // Destroy while exec_ctx is alive
    });
    read_thread.Start();

    Thread destroy_thread("destroy_thread", [&]() {
      read_started.WaitForNotification();
      // Small delay to increase chance of race
      absl::SleepFor(absl::Microseconds(10));
      ExecCtx exec_ctx;
      client_ep.reset();
    });
    destroy_thread.Start();

    read_thread.Join();
    destroy_thread.Join();

    // Explicitly destroy endpoints before unreffing calls
    client_ep.reset();
    server_ep.reset();
  }
}

TEST_F(SessionEndpointTest, FullDuplexConcurrency) {
  auto client_ep = CreateClientEndpoint();
  auto server_ep = CreateServerEndpoint();

  const int kNumIterations = 100;
  std::atomic<int> client_reads_done{0};
  std::atomic<int> client_writes_done{0};
  std::atomic<int> server_reads_done{0};
  std::atomic<int> server_writes_done{0};

  Notification client_read_finished;
  Notification client_write_finished;
  Notification server_read_finished;
  Notification server_write_finished;

  auto run_write_loop = [&](SessionEndpoint* ep, const std::string& msg,
                            std::atomic<int>& writes_done,
                            Notification& finished) {
    for (int i = 0; i < kNumIterations; ++i) {
      auto op_done = std::make_shared<Notification>();
      grpc_event_engine::experimental::SliceBuffer send_buffer;
      {
        ExecCtx exec_ctx;
        send_buffer.Append(
            grpc_event_engine::experimental::Slice::FromCopiedString(msg));
        ep->Write(
            [op_done, &writes_done](absl::Status status) {
              EXPECT_EQ(status, absl::OkStatus());
              writes_done.fetch_add(1, std::memory_order_release);
              op_done->Notify();
            },
            &send_buffer, SessionEndpoint::WriteArgs{});
        exec_ctx.Flush();
      }
      ASSERT_TRUE(op_done->WaitForNotificationWithTimeout(absl::Seconds(30)));
    }
    finished.Notify();
  };

  auto run_read_loop = [&](SessionEndpoint* ep, std::atomic<int>& reads_done,
                           Notification& finished) {
    for (int i = 0; i < kNumIterations; ++i) {
      auto op_done = std::make_shared<Notification>();
      grpc_event_engine::experimental::SliceBuffer recv_buffer;
      {
        ExecCtx exec_ctx;
        ep->Read(
            [op_done, &reads_done](absl::Status status) {
              EXPECT_EQ(status, absl::OkStatus());
              reads_done.fetch_add(1, std::memory_order_release);
              op_done->Notify();
            },
            &recv_buffer, SessionEndpoint::ReadArgs{});
        exec_ctx.Flush();
      }
      ASSERT_TRUE(op_done->WaitForNotificationWithTimeout(absl::Seconds(30)));
    }
    finished.Notify();
  };

  Thread client_write_thread("client_write", [&]() {
    run_write_loop(client_ep.get(), "client", client_writes_done,
                   client_write_finished);
  });

  Thread server_write_thread("server_write", [&]() {
    run_write_loop(server_ep.get(), "server", server_writes_done,
                   server_write_finished);
  });

  Thread client_read_thread("client_read", [&]() {
    run_read_loop(client_ep.get(), client_reads_done, client_read_finished);
  });

  Thread server_read_thread("server_read", [&]() {
    run_read_loop(server_ep.get(), server_reads_done, server_read_finished);
  });

  client_write_thread.Start();
  server_write_thread.Start();
  client_read_thread.Start();
  server_read_thread.Start();

  while (!client_read_finished.HasBeenNotified() ||
         !client_write_finished.HasBeenNotified() ||
         !server_read_finished.HasBeenNotified() ||
         !server_write_finished.HasBeenNotified()) {
    ExecCtx exec_ctx;
    grpc_completion_queue_next(cq_, grpc_timeout_milliseconds_to_deadline(10),
                               nullptr);
  }

  client_write_thread.Join();
  server_write_thread.Join();
  client_read_thread.Join();
  server_read_thread.Join();

  {
    ExecCtx exec_ctx;
    client_ep.reset();
    server_ep.reset();
  }

  EXPECT_EQ(client_writes_done.load(), kNumIterations);
  EXPECT_EQ(server_writes_done.load(), kNumIterations);
  EXPECT_EQ(client_reads_done.load(), kNumIterations);
  EXPECT_EQ(server_reads_done.load(), kNumIterations);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
