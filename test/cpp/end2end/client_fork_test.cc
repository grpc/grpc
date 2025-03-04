// Copyright 2022 The gRPC Authors
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

#include <grpc/support/port_platform.h>

#ifndef GRPC_ENABLE_FORK_SUPPORT
// No-op for builds without fork support.
int main(int /* argc */, char** /* argv */) { return 0; }
#else  // GRPC_ENABLE_FORK_SUPPORT

#include <grpc/fork.h>
#include <grpc/grpc.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <signal.h>

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "src/core/util/fork.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/util/test_config.h"

std::string GetBacktrace() {
  // constexpr std::string_view kBoop = "Boop";
  // std::array<void*, 10> ptrs;
  // int frame_count = absl::GetStackTrace(ptrs.data(), ptrs.size(), 1);
  // std::vector<std::string> frames(frame_count);
  // for (int i = 0; i < frame_count; ++i) {
  //   std::array<char, 150> txt;
  //   frames[i] = absl::StrCat("  ", i, " ",
  //                            absl::Symbolize(ptrs[i], txt.data(), txt.size())
  //                                ? std::string_view(txt.data(), txt.size())
  //                                : kBoop);
  // }
  // return absl::StrJoin(frames, "\n");
  return "";
}

namespace grpc {
namespace testing {
namespace {

class ServiceImpl final : public EchoTestService::Service {
  Status BidiStream(
      ServerContext* /*context*/,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    EchoRequest request;
    EchoResponse response;
    while (stream->Read(&request)) {
      LOG(INFO) << "[" << getpid() << "] recv msg " << request.message();
      response.set_message(request.message());
      stream->Write(response);
      LOG(INFO) << "[" << getpid() << "] wrote msg " << response.message();
    }
    return Status::OK;
  }
};

std::unique_ptr<EchoTestService::Stub> MakeStub(const std::string& addr) {
  return EchoTestService::NewStub(
      grpc::CreateChannel(addr, InsecureChannelCredentials()));
}

TEST(ClientForkTest, ClientCallsBeforeAndAfterForkSucceed) {
  grpc_core::Fork::Enable(true);

  int port = grpc_pick_unused_port_or_die();
  std::string addr = absl::StrCat("localhost:", port);

  LOG(INFO) << "[" << getpid() << "] Before first fork";
  pid_t server_pid = fork();
  switch (server_pid) {
    case -1:  // fork failed
      GTEST_FAIL() << "failure forking";
    case 0:  // post-fork child
    {
      LOG(INFO) << "[" << getpid() << "] After first fork in server 1";
      ServiceImpl impl;
      grpc::ServerBuilder builder;
      builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
      builder.RegisterService(&impl);
      LOG(INFO) << "[" << getpid() << "] After first fork in server 2";
      std::unique_ptr<Server> server(builder.BuildAndStart());
      LOG(INFO) << "[" << getpid() << "] After first fork in server 3";
      server->Wait();
      return;
    }
    default:  // post-fork parent
      break;
  }
  LOG(INFO) << "[" << getpid() << "] After first fork in client";
  // Do a round trip before we fork.
  // NOTE: without this scope, test running with the epoll1 poller will fail.
  {
    LOG(INFO) << "[" << getpid() << "] After first fork in client 1";
    std::unique_ptr<EchoTestService::Stub> stub = MakeStub(addr);
    LOG(INFO) << "[" << getpid() << "] After first fork in client 2";
    EchoRequest request;
    EchoResponse response;
    ClientContext context;
    context.set_wait_for_ready(true);

    LOG(INFO) << "[" << getpid() << "] After first fork in client 3";
    auto stream = stub->BidiStream(&context);
    LOG(INFO) << "[" << getpid() << "] After first fork in client 4";

    request.set_message("Hello");
    ASSERT_TRUE(stream->Write(request));
    ASSERT_TRUE(stream->Read(&response));
    ASSERT_EQ(response.message(), request.message());
  }
  // Fork and do round trips in the post-fork parent and child.
  LOG(INFO) << "[" << getpid() << "] Before fork 2";
  pid_t child_client_pid = fork();
  LOG(INFO) << "[" << getpid() << "] After fork 2";
  switch (child_client_pid) {
    case -1:  // fork failed
      GTEST_FAIL() << "fork failed";
    case 0:  // post-fork child
    {
      VLOG(2) << "In post-fork child";
      EchoRequest request;
      EchoResponse response;
      ClientContext context;
      context.set_wait_for_ready(true);

      std::unique_ptr<EchoTestService::Stub> stub = MakeStub(addr);
      auto stream = stub->BidiStream(&context);

      request.set_message("Hello again from child");
      ASSERT_TRUE(stream->Write(request));
      ASSERT_TRUE(stream->Read(&response));
      ASSERT_EQ(response.message(), request.message());
      exit(0);
    }
    default:  // post-fork parent
    {
      VLOG(2) << "In post-fork parent";
      EchoRequest request;
      EchoResponse response;
      ClientContext context;
      context.set_wait_for_ready(true);

      std::unique_ptr<EchoTestService::Stub> stub = MakeStub(addr);
      auto stream = stub->BidiStream(&context);

      request.set_message("Hello again from parent");
      EXPECT_TRUE(stream->Write(request));
      EXPECT_TRUE(stream->Read(&response));
      EXPECT_EQ(response.message(), request.message());

      // Wait for the post-fork child to exit; ensure it exited cleanly.
      int child_status;
      ASSERT_EQ(waitpid(child_client_pid, &child_status, 0), child_client_pid)
          << "failed to get status of child client";
      ASSERT_EQ(WEXITSTATUS(child_status), 0) << "child did not exit cleanly";
    }
  }

  kill(server_pid, SIGINT);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  absl::InitializeSymbolizer(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int res = RUN_ALL_TESTS();
  grpc_shutdown();
  return res;
}
#endif  // GRPC_ENABLE_FORK_SUPPORT
