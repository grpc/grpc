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
  ClientContext context;
  Empty request;
  Empty response;
  Status status;
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
  CompletionQueue cq;
  HookService::Stub stub(std::move(channel));
  auto call = stub.PrepareAsyncHook(&info.context, info.request, &cq);
  call->StartCall();
  call->Finish(&info.response, &info.status, &info);
  ASSERT_EQ(server.ExpectRequests(1), 1);
  server.Return(StatusCode::INTERNAL, "Just a test");
  void* returned_tag;
  bool ok = false;
  ASSERT_TRUE(cq.Next(&returned_tag, &ok));
  EXPECT_TRUE(ok);
  EXPECT_EQ(returned_tag, &info);
  EXPECT_EQ(info.status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(info.status.error_message(), "Just a test");
  cq.Shutdown();
}

TEST(PreStopHookServer, StartedWhileRunning) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status status = server.Start(port, 15);
  ASSERT_TRUE(status.ok()) << status.error_message();
  status = server.Start(port, 15);
  ASSERT_EQ(status.error_code(), StatusCode::ALREADY_EXISTS)
      << status.error_message();
}

TEST(PreStopHookServer, ClosingWhilePending) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status start_status = server.Start(port, 15);
  ASSERT_TRUE(start_status.ok()) << start_status.error_message();
  auto channel = CreateChannel(absl::StrFormat("127.0.0.1:%d", port),
                               InsecureChannelCredentials());
  ASSERT_TRUE(channel);
  CompletionQueue cq;
  CallInfo info;
  HookService::Stub stub(std::move(channel));
  auto call = stub.PrepareAsyncHook(&info.context, info.request, &cq);
  call->StartCall();
  call->Finish(&info.response, &info.status, &info);
  ASSERT_EQ(server.ExpectRequests(1), 1);
  ASSERT_TRUE(server.Stop().ok());
  void* returned_tag;
  bool ok = false;
  ASSERT_TRUE(cq.Next(&returned_tag, &ok));
  EXPECT_TRUE(ok);
  EXPECT_EQ(returned_tag, &info);
  cq.Shutdown();
  EXPECT_EQ(info.status.error_code(), StatusCode::ABORTED);
}

TEST(PreStopHookServer, MultiplePending) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status start_status = server.Start(port, 15);
  ASSERT_TRUE(start_status.ok()) << start_status.error_message();
  auto channel = CreateChannel(absl::StrFormat("127.0.0.1:%d", port),
                               InsecureChannelCredentials());
  ASSERT_TRUE(channel);
  std::array<CallInfo, 2> info;
  CompletionQueue cq;
  HookService::Stub stub(std::move(channel));
  auto call1 = stub.PrepareAsyncHook(&info[0].context, info[0].request, &cq);
  call1->StartCall();
  call1->Finish(&info[0].response, &info[0].status, &info[0]);
  auto call2 = stub.PrepareAsyncHook(&info[1].context, info[1].request, &cq);
  call2->StartCall();
  call2->Finish(&info[1].response, &info[1].status, &info[1]);
  server.ExpectRequests(2);
  server.Return(StatusCode::INTERNAL, "Just a test");
  std::array<void*, 2> tags;
  std::array<bool, 2> oks;
  ASSERT_TRUE(cq.Next(&tags[0], &oks[0]));
  ASSERT_TRUE(cq.Next(&tags[1], &oks[1]));
  EXPECT_THAT(tags, ::testing::UnorderedElementsAre(&info[0], &info[1]));
  EXPECT_THAT(oks, ::testing::ElementsAre(true, true));
  EXPECT_EQ(info[0].status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(info[0].status.error_message(), "Just a test");
  EXPECT_EQ(info[1].status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(info[1].status.error_message(), "Just a test");
  cq.Shutdown();
}

TEST(PreStopHookServer, StoppingNotStarted) {
  PreStopHookServerManager server;
  Status status = server.Stop();
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE)
      << status.error_message();
}

TEST(PreStopHookServer, ReturnBeforeSend) {
  int port = grpc_pick_unused_port_or_die();
  PreStopHookServerManager server;
  Status start_status = server.Start(port, 15);
  server.Return(StatusCode::INTERNAL, "Just a test");
  ASSERT_TRUE(start_status.ok()) << start_status.error_message();
  auto channel = CreateChannel(absl::StrFormat("127.0.0.1:%d", port),
                               InsecureChannelCredentials());
  ASSERT_TRUE(channel);
  CallInfo info;
  CompletionQueue cq;
  HookService::Stub stub(std::move(channel));
  auto call = stub.PrepareAsyncHook(&info.context, info.request, &cq);
  call->StartCall();
  call->Finish(&info.response, &info.status, &info);
  void* returned_tag;
  bool ok = false;
  ASSERT_TRUE(cq.Next(&returned_tag, &ok));
  EXPECT_TRUE(ok);
  EXPECT_EQ(returned_tag, &info);
  EXPECT_EQ(info.status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(info.status.error_message(), "Just a test");
  cq.Shutdown();
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
