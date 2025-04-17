//
//
// Copyright 2018 gRPC authors.
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

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <memory>

#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "src/core/lib/iomgr/port.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

#ifdef GRPC_POSIX_SOCKET
#include "src/core/lib/iomgr/ev_posix.h"
#endif  // GRPC_POSIX_SOCKET

#include "gtest/gtest.h"

#ifdef GRPC_POSIX_SOCKET
// Thread-local variable to so that only polls from this test assert
// non-blocking (not polls from resolver, timer thread, etc), and only when the
// thread is waiting on polls caused by CompletionQueue::AsyncNext (not for
// picking a port or other reasons).
static thread_local bool g_is_nonblocking_poll;

namespace {

int maybe_assert_non_blocking_poll(struct pollfd* pfds, nfds_t nfds,
                                   int timeout) {
  // Only assert that this poll should have zero timeout if we're in the
  // middle of a zero-timeout CQ Next.
  if (g_is_nonblocking_poll) {
    CHECK_EQ(timeout, 0);
  }
  return poll(pfds, nfds, timeout);
}

}  // namespace

namespace grpc {
namespace testing {
namespace {

void* tag(int i) { return reinterpret_cast<void*>(static_cast<intptr_t>(i)); }
int detag(void* p) { return static_cast<int>(reinterpret_cast<intptr_t>(p)); }

class NonblockingTest : public ::testing::Test {
 protected:
  NonblockingTest() {}

  void SetUp() override {
    port_ = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port_;

    // Setup server
    BuildAndStartServer();
  }

  bool LoopForTag(void** tag, bool* ok) {
    // Temporarily set the thread-local nonblocking poll flag so that the polls
    // caused by this loop are indeed sent by the library with zero timeout.
    bool orig_val = g_is_nonblocking_poll;
    g_is_nonblocking_poll = true;
    for (;;) {
      auto r = cq_->AsyncNext(tag, ok, gpr_time_0(GPR_CLOCK_REALTIME));
      if (r == CompletionQueue::SHUTDOWN) {
        g_is_nonblocking_poll = orig_val;
        return false;
      } else if (r == CompletionQueue::GOT_EVENT) {
        g_is_nonblocking_poll = orig_val;
        return true;
      }
    }
  }

  void TearDown() override {
    server_->Shutdown();
    void* ignored_tag;
    bool ignored_ok;
    cq_->Shutdown();
    while (LoopForTag(&ignored_tag, &ignored_ok)) {
    }
    stub_.reset();
    grpc_recycle_unused_port(port_);
  }

  void BuildAndStartServer() {
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             grpc::InsecureServerCredentials());
    service_ = std::make_unique<grpc::testing::EchoTestService::AsyncService>();
    builder.RegisterService(service_.get());
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
  }

  void ResetStub() {
    std::shared_ptr<Channel> channel = grpc::CreateChannel(
        server_address_.str(), grpc::InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

  void SendRpc(int num_rpcs) {
    for (int i = 0; i < num_rpcs; i++) {
      EchoRequest send_request;
      EchoRequest recv_request;
      EchoResponse send_response;
      EchoResponse recv_response;
      Status recv_status;

      ClientContext cli_ctx;
      ServerContext srv_ctx;
      grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

      send_request.set_message("hello non-blocking world");
      std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
          stub_->PrepareAsyncEcho(&cli_ctx, send_request, cq_.get()));

      response_reader->StartCall();
      response_reader->Finish(&recv_response, &recv_status, tag(4));

      service_->RequestEcho(&srv_ctx, &recv_request, &response_writer,
                            cq_.get(), cq_.get(), tag(2));

      void* got_tag;
      bool ok;
      EXPECT_TRUE(LoopForTag(&got_tag, &ok));
      EXPECT_TRUE(ok);
      EXPECT_EQ(detag(got_tag), 2);
      EXPECT_EQ(send_request.message(), recv_request.message());

      send_response.set_message(recv_request.message());
      response_writer.Finish(send_response, Status::OK, tag(3));

      int tagsum = 0;
      int tagprod = 1;
      EXPECT_TRUE(LoopForTag(&got_tag, &ok));
      EXPECT_TRUE(ok);
      tagsum += detag(got_tag);
      tagprod *= detag(got_tag);

      EXPECT_TRUE(LoopForTag(&got_tag, &ok));
      EXPECT_TRUE(ok);
      tagsum += detag(got_tag);
      tagprod *= detag(got_tag);

      EXPECT_EQ(tagsum, 7);
      EXPECT_EQ(tagprod, 12);
      EXPECT_EQ(send_response.message(), recv_response.message());
      EXPECT_TRUE(recv_status.ok());
    }
  }

  std::unique_ptr<ServerCompletionQueue> cq_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<grpc::testing::EchoTestService::AsyncService> service_;
  std::ostringstream server_address_;
  int port_;
};

TEST_F(NonblockingTest, SimpleRpc) {
  ResetStub();
  SendRpc(10);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

#endif  // GRPC_POSIX_SOCKET

int main(int argc, char** argv) {
#ifdef GRPC_POSIX_SOCKET
  // Override the poll function before anything else can happen
  grpc_poll_function = maybe_assert_non_blocking_poll;

  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);

  // Start the nonblocking poll thread-local variable as false because the
  // thread that issues RPCs starts by picking a port (which has non-zero
  // timeout).
  g_is_nonblocking_poll = false;

  int ret = RUN_ALL_TESTS();

  return ret;
#else   // GRPC_POSIX_SOCKET
  (void)argc;
  (void)argv;
  return 0;
#endif  // GRPC_POSIX_SOCKET
}
