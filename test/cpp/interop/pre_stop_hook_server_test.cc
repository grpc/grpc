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

  absl::optional<Status> WaitForStatus(
      absl::Duration timeout = absl::Seconds(1)) {
    grpc_core::MutexLock lock(&mu);
    if (!status_.has_value()) {
      cv.WaitWithTimeout(&mu, timeout);
    }
    return status_;
  }

  void SetStatus(const Status& status) {
    grpc_core::MutexLock lock(&mu);
    status_ = status;
    cv.SignalAll();
  }

  bool Completed() { return WaitForStatus(absl::ZeroDuration()).has_value(); }

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
  ASSERT_TRUE(server.TestOnlyExpectRequests(1));
  server.Return(StatusCode::INTERNAL, "Just a test");
  auto status = info.WaitForStatus();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status->error_message(), "Just a test");
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
  ASSERT_EQ(server.TestOnlyExpectRequests(1), 1);
  ASSERT_TRUE(server.Stop().ok());
  auto status = info.WaitForStatus();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->error_code(), StatusCode::ABORTED);
}

TEST(PreStopHookServer, MultipleRequests) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status start_status = server.Start(port, 15);
  ASSERT_TRUE(start_status.ok()) << start_status.error_message();
  auto channel = CreateChannel(absl::StrFormat("127.0.0.1:%d", port),
                               InsecureChannelCredentials());
  ASSERT_TRUE(channel);
  HookService::Stub stub(std::move(channel));
  CallInfo info1, info2, info3;
  stub.async()->Hook(&info1.context, &info1.request, &info1.response,
                     [&](Status status) { info1.SetStatus(status); });
  ASSERT_FALSE(info1.Completed());
  stub.async()->Hook(&info2.context, &info2.request, &info2.response,
                     [&](Status status) { info2.SetStatus(status); });
  ASSERT_FALSE(info2.Completed());
  server.Return(StatusCode::RESOURCE_EXHAUSTED, "Second");
  auto status = info1.WaitForStatus();
  ASSERT_TRUE(status.has_value());
  ASSERT_EQ(status->error_code(), StatusCode::RESOURCE_EXHAUSTED);
  status = info2.WaitForStatus();
  ASSERT_EQ(status->error_code(), StatusCode::RESOURCE_EXHAUSTED);
  stub.async()->Hook(&info3.context, &info3.request, &info3.response,
                     [&](Status status) { info3.SetStatus(status); });
  status = info3.WaitForStatus();
  ASSERT_EQ(status->error_code(), StatusCode::RESOURCE_EXHAUSTED);
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
  auto status = info.WaitForStatus();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status->error_message(), "Just a test");
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
