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

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "src/core/util/fork.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/util/test_config.h"

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
      LOG(INFO) << "recv msg " << request.message();
      response.set_message(request.message());
      stream->Write(response);
      LOG(INFO) << "wrote msg " << response.message();
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

  pid_t server_pid = fork();
  switch (server_pid) {
    case -1:  // fork failed
      GTEST_FAIL() << "failure forking";
    case 0:  // post-fork child
    {
      ServiceImpl impl;
      grpc::ServerBuilder builder;
      builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
      builder.RegisterService(&impl);
      std::unique_ptr<Server> server(builder.BuildAndStart());
      server->Wait();
      return;
    }
    default:  // post-fork parent
      break;
  }

  // Do a round trip before we fork.
  // NOTE: without this scope, test running with the epoll1 poller will fail.
  {
    std::unique_ptr<EchoTestService::Stub> stub = MakeStub(addr);
    EchoRequest request;
    EchoResponse response;
    ClientContext context;
    context.set_wait_for_ready(true);

    auto stream = stub->BidiStream(&context);

    request.set_message("Hello");
    ASSERT_TRUE(stream->Write(request));
    ASSERT_TRUE(stream->Read(&response));
    ASSERT_EQ(response.message(), request.message());
  }
  // Fork and do round trips in the post-fork parent and child.
  pid_t child_client_pid = fork();
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
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int res = RUN_ALL_TESTS();
  grpc_shutdown();
  return res;
}
#endif  // GRPC_ENABLE_FORK_SUPPORT
