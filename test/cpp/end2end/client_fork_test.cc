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

#include <cstring>

#include <grpc/support/port_platform.h>

#include "src/proto/grpc/testing/echo_messages.pb.h"

#ifndef GRPC_ENABLE_FORK_SUPPORT
// No-op for builds without fork support.
int main(int /* argc */, char** /* argv */) { return 0; }
#else  // GRPC_ENABLE_FORK_SUPPORT

#include <signal.h>

#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"

#include <grpc/fork.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/gprpp/fork.h"
#include "src/core/lib/gprpp/sync.h"
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
      gpr_log(GPR_INFO, "recv msg %s", request.message().c_str());
      response.set_message(request.message());
      stream->Write(response);
      gpr_log(GPR_INFO, "wrote msg %s", response.message().c_str());
    }
    return Status::OK;
  }
};

std::unique_ptr<EchoTestService::Stub> MakeStub(const std::string& addr) {
  return EchoTestService::NewStub(
      grpc::CreateChannel(addr, InsecureChannelCredentials()));
}

void RunServer(const std::string& addr, int read_pipe_fd) {
  gpr_log(GPR_INFO, "Server pid: %d", getpid());
  int code = 0;
  bool wait = true;
  while (wait) {
    int r = read(read_pipe_fd, &code, sizeof(code));
    ASSERT_GE(r, 0) << strerror(errno);
    wait = r == 0;
  }
  gpr_log(GPR_INFO, "Server start");
  ServiceImpl impl;
  grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&impl);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  server->Wait();
}

void SendMessage(const std::string& addr, const std::string& message) {
  std::unique_ptr<EchoTestService::Stub> stub = MakeStub(addr);
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_wait_for_ready(true);
  auto stream = stub->BidiStream(&context);
  request.set_message(message);
  ASSERT_TRUE(stream->Write(request));
  ASSERT_TRUE(stream->Read(&response));
  ASSERT_EQ(response.message(), request.message());
}

void SendAsyncMessageAndNotifyServer(const std::string& addr,
                                     const std::string& message,
                                     int write_pipe_fd) {
  class Reactor : public ClientBidiReactor<EchoRequest, EchoResponse> {
   public:
    void OnDone(const Status& status) override {
      grpc_core::MutexLock lock(&mu_);
      status_ = status;
      cond_.SignalAll();
    }

    Status AwaitDone() {
      grpc_core::MutexLock lock(&mu_);
      while (!status_.has_value()) {
        cond_.Wait(&mu_);
      }
      return *status_;
    }

   private:
    grpc_core::Mutex mu_;
    grpc_core::CondVar cond_;
    absl::optional<Status> status_ ABSL_GUARDED_BY(mu_);
  } reactor;

  auto stub = EchoTestService::NewStub(
      grpc::CreateChannel(addr, InsecureChannelCredentials()));
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_wait_for_ready(true);
  request.set_message("Initial request");
  stub->async()->BidiStream(&context, &reactor);
  reactor.StartWriteLast(&request, WriteOptions());
  reactor.StartRead(&response);
  reactor.StartCall();
  int code = 1;
  sleep(1);
  ASSERT_GT(write(write_pipe_fd, &code, sizeof(code)), 0) << strerror(errno);
  Status status = reactor.AwaitDone();
  ASSERT_TRUE(status.ok()) << absl::StrFormat("[%d] %s", status.error_code(),
                                              status.error_message());
  ASSERT_EQ(response.message(), request.message());
  gpr_log(GPR_INFO, "Initial exchange done!");
}

TEST(ClientForkTest, ClientCallsBeforeAndAfterForkSucceed) {
  grpc_core::Fork::Enable(true);
  int port = grpc_pick_unused_port_or_die();
  std::string addr = absl::StrCat("localhost:", port);
  gpr_log(GPR_INFO, "Test pid: %d", getpid());
  int fds[2];
  pipe(fds);
  pid_t server_pid = fork();
  ASSERT_NE(server_pid, -1) << "fork failed";
  // Run server in child
  if (server_pid == 0) {
    close(fds[1]);
    RunServer(addr, fds[0]);
    close(fds[0]);
    exit(0);
  }
  // Reproducing the bug where having a failed channel becomes
  // an issue post-fork
  close(fds[0]);
  SendAsyncMessageAndNotifyServer(addr, "Hello", fds[1]);
  close(fds[1]);
  // Fork and do round trips in the post-fork parent and child.
  gpr_log(GPR_INFO, "About to fork (%d)", getpid());
  pid_t child_client_pid = fork();
  ASSERT_NE(child_client_pid, -1) << "fork failed";
  gpr_log(GPR_INFO, "After fork - before wait (%d)", getpid());
  if (child_client_pid != 0) {
    sleep(1);
  }
  gpr_log(GPR_INFO, "After fork - after wait (%d)", getpid());
  SendMessage(addr, child_client_pid == 0 ? "Hello again from child"
                                          : "Hello again from parent");
  // Cleanup childe processes from parent
  if (child_client_pid != 0) {
    // Wait for the post-fork child to exit; ensure it exited cleanly.
    int child_status;
    ASSERT_EQ(waitpid(child_client_pid, &child_status, 0), child_client_pid)
        << "failed to get status of child client";
    ASSERT_EQ(WEXITSTATUS(child_status), 0) << "child did not exit cleanly";
    kill(server_pid, SIGINT);
  }
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
