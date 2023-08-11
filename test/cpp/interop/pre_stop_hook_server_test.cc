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

#include <map>
#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

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

  Status WaitForStatus() {
    grpc_core::MutexLock lock(&mu);
    while (!status_.has_value()) {
      cv.Wait(&mu);
    }
    return *status_;
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

TEST(PreStopHookServer, StartDoRequestStop) {
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
                     [&info](Status status) { info.SetStatus(status); });
  ASSERT_EQ(server.ExpectRequests(1), 1);
  server.Return(StatusCode::INTERNAL, "Just a test");
  Status status = info.WaitForStatus();
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "Just a test");
}

TEST(PreStopHookServer, StartServerWhileAlreadyRunning) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status status = server.Start(port, 15);
  ASSERT_TRUE(status.ok()) << status.error_message();
  status = server.Start(port, 15);
  ASSERT_EQ(status.error_code(), StatusCode::ALREADY_EXISTS)
      << status.error_message();
}

TEST(PreStopHookServer, StopServerWhileRequestPending) {
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
                     [&info](Status status) { info.SetStatus(status); });
  ASSERT_EQ(server.ExpectRequests(1), 1);
  ASSERT_TRUE(server.Stop().ok());
  Status status = info.WaitForStatus();
  EXPECT_EQ(status.error_code(), StatusCode::ABORTED);
}

TEST(PreStopHookServer, RespondToMultiplePendingRequests) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status start_status = server.Start(port, 15);
  ASSERT_TRUE(start_status.ok()) << start_status.error_message();
  auto channel = CreateChannel(absl::StrFormat("127.0.0.1:%d", port),
                               InsecureChannelCredentials());
  ASSERT_TRUE(channel);
  std::array<CallInfo, 2> info;
  HookService::Stub stub(std::move(channel));
  stub.async()->Hook(&info[0].context, &info[0].request, &info[0].response,
                     [&info](Status status) { info[0].SetStatus(status); });
  stub.async()->Hook(&info[1].context, &info[1].request, &info[1].response,
                     [&info](Status status) { info[1].SetStatus(status); });
  server.ExpectRequests(2);
  server.Return(StatusCode::INTERNAL, "Just a test");
  Status status = info[0].WaitForStatus();
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "Just a test");
  status = info[1].WaitForStatus();
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "Just a test");
}

TEST(PreStopHookServer, StopServerThatNotStarted) {
  PreStopHookServerManager server;
  Status status = server.Stop();
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE)
      << status.error_message();
}

TEST(PreStopHookServer, SetStatusBeforeRequestReceived) {
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
  stub.async()->Hook(&info.context, &info.request, &info.response,
                     [&info](Status status) { info.SetStatus(status); });
  Status status = info.WaitForStatus();
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "Just a test");
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
