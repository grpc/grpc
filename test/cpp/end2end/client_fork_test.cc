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
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <gtest/gtest.h>
#include <signal.h>
#include <unistd.h>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/fork.h"
#include "src/core/util/sync.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/util/test_config.h"

namespace grpc::testing {
namespace {

class EchoClientBidiReactor
    : public grpc::ClientBidiReactor<EchoRequest, EchoResponse> {
 public:
  void OnDone(const grpc::Status& /*s*/) override {
    VLOG(2) << "[" << getpid() << "] Everything done";
    grpc_core::MutexLock lock(&mu_);
    all_done_ = true;
    cond_.SignalAll();
  }

  void OnReadDone(bool ok) override {
    VLOG(2) << "[" << getpid() << "] Read done: " << ok;
    grpc_core::MutexLock lock(&mu_);
    read_ = true;
    cond_.SignalAll();
  }

  void OnWriteDone(bool ok) override {
    VLOG(2) << "[" << getpid() << "] Async client write done: " << ok;
    grpc_core::MutexLock lock(&mu_);
    write_ = true;
    cond_.SignalAll();
  }

  void WaitReadWriteDone() {
    grpc_core::MutexLock lock(&mu_);
    while (!read_ || !write_) {
      cond_.Wait(&mu_);
    }
  }

  void WaitAllDone() {
    grpc_core::MutexLock lock(&mu_);
    while (!all_done_) {
      cond_.Wait(&mu_);
    }
  }

 private:
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  bool read_ ABSL_GUARDED_BY(&mu_) = false;
  bool write_ ABSL_GUARDED_BY(&mu_) = false;
  bool all_done_ ABSL_GUARDED_BY(&mu_) = false;
};

class ServiceImpl final : public EchoTestService::Service {
  Status BidiStream(
      ServerContext* /*context*/,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    EchoRequest request;
    EchoResponse response;
    while (stream->Read(&request)) {
      VLOG(2) << "[" << getpid() << "] Server recv msg " << request.message();
      response.set_message(request.message());
      stream->Write(response);
      VLOG(2) << "[" << getpid() << "] Server wrote msg " << response.message();
    }
    return Status::OK;
  }
};

std::pair<std::string, std::string> DoExchangeAsync(absl::string_view label,
                                                    const std::string& addr) {
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_wait_for_ready(true);
  std::unique_ptr<EchoTestService::Stub> stub = EchoTestService::NewStub(
      grpc::CreateChannel(addr, InsecureChannelCredentials()));
  EchoClientBidiReactor reactor;
  stub->async()->BidiStream(&context, &reactor);
  request.set_message("Hello again from child");
  reactor.StartWrite(&request);
  reactor.StartRead(&response);
  reactor.StartCall();
  VLOG(2) << label << " Doing the call";
  reactor.WaitReadWriteDone();
  reactor.StartWritesDone();
  reactor.WaitAllDone();
  return {response.message(), request.message()};
}

std::pair<std::string, std::string> DoExchangeSync(absl::string_view label,
                                                   const std::string& addr) {
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_wait_for_ready(true);
  std::unique_ptr<EchoTestService::Stub> stub = EchoTestService::NewStub(
      grpc::CreateChannel(addr, InsecureChannelCredentials()));
  auto stream = stub->BidiStream(&context);
  request.set_message("Hello again from child");
  stream->Write(request);
  stream->Read(&response);
  return {response.message(), request.message()};
}

void DoExchange(
    absl::string_view label, const std::string& addr,
    grpc_core::SourceLocation location = grpc_core::SourceLocation()) {
  const auto& [response, request] = DoExchangeAsync(label, addr);
  EXPECT_EQ(response, request) << absl::StrCat(location);
  const auto& [response_sync, request_sync] = DoExchangeSync(label, addr);
  EXPECT_EQ(response_sync, request_sync) << absl::StrCat(location);
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
      VLOG(2) << "[" << getpid() << "] Starting server post first fork";
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
  VLOG(2) << "[" << getpid() << "] ###########  First fork ##########";
  VLOG(2) << "[" << getpid() << "] ######  Client post fork 1  ######";
  VLOG(2) << "[" << getpid() << "] ##################################";
  // Do a round trip before we fork.
  // NOTE: without this scope, test running with the epoll1 poller will fail.
  DoExchange(absl::StrCat("[", getpid(), "] In first-fork parent"), addr);
  // Fork and do round trips in the post-fork parent and child.
  pid_t child_client_pid = fork();
  switch (child_client_pid) {
    case -1:  // fork failed
      GTEST_FAIL() << "fork failed";
    case 0:  // post-fork child
    {
      VLOG(2) << "[" << getpid() << "] In post-fork child";
      DoExchange(absl::StrCat("[", getpid(), "] In post-fork child"), addr);
      exit(0);
    }
    default:  // post-fork parent
    {
      VLOG(2) << "[" << getpid() << "] ##########  Second fork  ##########";
      VLOG(2) << "[" << getpid() << "] ######  In post-fork parent  ######";
      VLOG(2) << "[" << getpid() << "] ###################################";
      DoExchange(absl::StrCat("[", getpid(), "] In post-fork parent"), addr);
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
}  // namespace grpc::testing

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
