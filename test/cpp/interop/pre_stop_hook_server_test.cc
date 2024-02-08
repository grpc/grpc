//
// Copyright 2023 gRPC authors.
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

#include "test/cpp/interop/pre_stop_hook_server.h"

#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

struct CallInfo {
 public:
  ClientContext context;
  Empty request;
  Empty response;

  absl::optional<Status> WaitForStatus(
      absl::Duration timeout = absl::Seconds(1)) {
    grpc_core::MutexLock lock(&mu);
    cv.WaitWithTimeout(&mu, timeout);
    return status_;
  }

  void SetStatus(const Status& status) {
    grpc_core::MutexLock lock(&mu);
    status_ = status;
    cv.SignalAll();
  }

 private:
  grpc_core::Mutex mu;
  grpc_core::CondVar cv;
  absl::optional<Status> status_;
};

void ServerLoop(HookServiceImpl* service, int port, Server** server,
                grpc_core::Mutex* mu, grpc_core::CondVar* condition) {
  ServerBuilder builder;
  builder.AddListeningPort(absl::StrFormat("0.0.0.0:%d", port),
                           grpc::InsecureServerCredentials());
  builder.RegisterService(service);
  auto s = builder.BuildAndStart();
  {
    grpc_core::MutexLock lock(mu);
    *server = s.get();
    condition->SignalAll();
  }
  s->Wait();
}

TEST(StandalonePreStopHookServer, StartDoRequestStop) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status start_status = server.Start(port, 15);
  ASSERT_TRUE(start_status.ok()) << start_status.error_message();
  auto channel = CreateChannel(absl::StrFormat("127.0.0.1:%d", port),
                               InsecureChannelCredentials());
  ASSERT_TRUE(channel);
  CallInfo info;
  HookService::Stub stub(std::move(channel));
  stub.async()->Hook(&info.context, &info.request, &info.response,
                     [&info](const Status& status) { info.SetStatus(status); });
  ASSERT_TRUE(server.TestOnlyExpectRequests(1));
  server.Return(StatusCode::INTERNAL, "Just a test");
  auto status = info.WaitForStatus();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status->error_message(), "Just a test");
}

TEST(StandalonePreStopHookServer, StartServerWhileAlreadyRunning) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status status = server.Start(port, 15);
  ASSERT_TRUE(status.ok()) << status.error_message();
  status = server.Start(port, 15);
  ASSERT_EQ(status.error_code(), StatusCode::ALREADY_EXISTS)
      << status.error_message();
}

TEST(StandalonePreStopHookServer, StopServerWhileRequestPending) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status start_status = server.Start(port, 15);
  ASSERT_TRUE(start_status.ok()) << start_status.error_message();
  auto channel = CreateChannel(absl::StrFormat("127.0.0.1:%d", port),
                               InsecureChannelCredentials());
  ASSERT_TRUE(channel);
  CallInfo info;
  HookService::Stub stub(std::move(channel));
  stub.async()->Hook(&info.context, &info.request, &info.response,
                     [&info](const Status& status) { info.SetStatus(status); });
  ASSERT_TRUE(server.TestOnlyExpectRequests(1));
  ASSERT_TRUE(server.Stop().ok());
  auto status = info.WaitForStatus();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->error_code(), StatusCode::ABORTED);
}

TEST(StandalonePreStopHookServer, MultipleRequests) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status start_status = server.Start(port, 15);
  ASSERT_TRUE(start_status.ok()) << start_status.error_message();
  auto channel = CreateChannel(absl::StrFormat("127.0.0.1:%d", port),
                               InsecureChannelCredentials());
  ASSERT_TRUE(channel);
  HookService::Stub stub(std::move(channel));
  CallInfo info1, info2, info3;
  server.Return(StatusCode::INTERNAL, "First");
  stub.async()->Hook(&info1.context, &info1.request, &info1.response,
                     [&](const Status& status) { info1.SetStatus(status); });
  auto status = info1.WaitForStatus();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status->error_message(), "First");
  stub.async()->Hook(&info2.context, &info2.request, &info2.response,
                     [&](const Status& status) { info2.SetStatus(status); });
  ASSERT_TRUE(server.TestOnlyExpectRequests(1, absl::Milliseconds(500)));
  stub.async()->Hook(&info3.context, &info3.request, &info3.response,
                     [&](const Status& status) { info3.SetStatus(status); });
  server.Return(StatusCode::RESOURCE_EXHAUSTED, "Second");
  server.Return(StatusCode::DEADLINE_EXCEEDED, "Third");
  status = info2.WaitForStatus();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->error_code(), StatusCode::RESOURCE_EXHAUSTED);
  EXPECT_EQ(status->error_message(), "Second");
  status = info3.WaitForStatus();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->error_code(), StatusCode::DEADLINE_EXCEEDED);
  EXPECT_EQ(status->error_message(), "Third");
}

TEST(StandalonePreStopHookServer, StopServerThatNotStarted) {
  PreStopHookServerManager server;
  Status status = server.Stop();
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE)
      << status.error_message();
}

TEST(StandalonePreStopHookServer, SetStatusBeforeRequestReceived) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status start_status = server.Start(port, 15);
  server.Return(StatusCode::INTERNAL, "Just a test");
  ASSERT_TRUE(start_status.ok()) << start_status.error_message();
  auto channel = CreateChannel(absl::StrFormat("127.0.0.1:%d", port),
                               InsecureChannelCredentials());
  ASSERT_TRUE(channel);
  HookService::Stub stub(std::move(channel));
  CallInfo info;
  auto status = stub.Hook(&info.context, info.request, &info.response);
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "Just a test");
}

TEST(PreStopHookService, StartDoRequestStop) {
  int port = grpc_pick_unused_port_or_die();
  grpc_core::Mutex mu;
  grpc_core::CondVar condition;
  Server* server = nullptr;
  HookServiceImpl service;
  std::thread server_thread(ServerLoop, &service, port, &server, &mu,
                            &condition);
  {
    grpc_core::MutexLock lock(&mu);
    while (server == nullptr) {
      condition.Wait(&mu);
    }
  }
  auto channel = CreateChannel(absl::StrFormat("127.0.0.1:%d", port),
                               InsecureChannelCredentials());
  ASSERT_TRUE(channel);
  CallInfo infos[3];
  HookService::Stub stub(std::move(channel));
  stub.async()->Hook(
      &infos[0].context, &infos[0].request, &infos[0].response,
      [&infos](const Status& status) { infos[0].SetStatus(status); });
  stub.async()->Hook(
      &infos[1].context, &infos[1].request, &infos[1].response,
      [&infos](const Status& status) { infos[1].SetStatus(status); });
  ASSERT_TRUE(service.TestOnlyExpectRequests(2, absl::Milliseconds(100)));
  ClientContext ctx;
  SetReturnStatusRequest request;
  request.set_grpc_code_to_return(StatusCode::INTERNAL);
  request.set_grpc_status_description("Just a test");
  Empty response;
  ASSERT_EQ(stub.SetReturnStatus(&ctx, request, &response).error_code(),
            StatusCode::OK);
  auto status = infos[0].WaitForStatus();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status->error_message(), "Just a test");
  status = infos[1].WaitForStatus();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status->error_message(), "Just a test");
  status = stub.Hook(&infos[2].context, infos[2].request, &infos[2].response);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status->error_message(), "Just a test");
  CallInfo reset_call_info;
  ASSERT_TRUE(stub.ClearReturnStatus(&reset_call_info.context,
                                     reset_call_info.request,
                                     &reset_call_info.response)
                  .ok());
  CallInfo call_hangs;
  stub.async()->Hook(
      &call_hangs.context, &call_hangs.request, &call_hangs.response,
      [&](const Status& status) { call_hangs.SetStatus(status); });
  ASSERT_TRUE(service.TestOnlyExpectRequests(1, absl::Milliseconds(100)));
  status = call_hangs.WaitForStatus(absl::Milliseconds(100));
  EXPECT_FALSE(status.has_value()) << status->error_message();
  service.Stop();
  EXPECT_EQ(call_hangs.WaitForStatus().value_or(Status::CANCELLED).error_code(),
            StatusCode::ABORTED);
  server->Shutdown();
  server_thread.join();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
