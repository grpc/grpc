//
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
//
//

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/strings/match.h"

namespace grpc {
namespace testing {
namespace {

inline void* tag(int i) {
  return reinterpret_cast<void*>(static_cast<intptr_t>(i));
}

class TestServiceImpl : public EchoTestService::Service {
 public:
  Status Echo(ServerContext* /*context*/, const EchoRequest* /*request*/,
              EchoResponse* response) override {
    response->set_message("response");
    return Status::OK;
  }

  Status ResponseStream(ServerContext* /*context*/,
                        const EchoRequest* /*request*/,
                        ServerWriter<EchoResponse>* writer) override {
    EchoResponse response;
    response.set_message("response");
    writer->Write(response);
    return Status::OK;
  }
};

class CardinalityViolationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    int port = 0;
    ServerBuilder builder;
    builder.AddListeningPort("localhost:0", InsecureServerCredentials(), &port);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    server_address_ << "localhost:" << port;
    channel_ = grpc::CreateCustomChannel(server_address_.str(),
                                         InsecureChannelCredentials(),
                                         ChannelArguments());
  }

  void TearDown() override { server_->Shutdown(); }

  std::shared_ptr<Channel> channel_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  TestServiceImpl service_;
};

TEST_F(CardinalityViolationTest, UnaryZeroRequests) {
  const char* experiments = std::getenv("GRPC_EXPERIMENTS");
  if (experiments != nullptr &&
      absl::StrContains(experiments, "promise_based_http2_client_transport")) {
    GTEST_SKIP() << "Skipping due to promise based transport limitation";
  }
  ClientContext context;
  Status status;
  GenericStub generic_stub(channel_);
  CompletionQueue cq;
  std::unique_ptr<GenericClientAsyncReaderWriter> call =
      generic_stub.PrepareCall(&context, "/grpc.testing.EchoTestService/Echo",
                               &cq);
  call->StartCall(tag(1));
  void* got_tag;
  bool ok;
  gpr_timespec deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                       gpr_time_from_seconds(10, GPR_TIMESPAN));
  EXPECT_EQ(cq.AsyncNext(&got_tag, &ok, deadline), CompletionQueue::GOT_EVENT);
  EXPECT_EQ(got_tag, tag(1));
  call->WritesDone(tag(2));
  deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                          gpr_time_from_seconds(10, GPR_TIMESPAN));
  EXPECT_EQ(cq.AsyncNext(&got_tag, &ok, deadline), CompletionQueue::GOT_EVENT);
  EXPECT_EQ(got_tag, tag(2));
  call->Finish(&status, tag(3));
  deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                          gpr_time_from_seconds(10, GPR_TIMESPAN));
  EXPECT_EQ(cq.AsyncNext(&got_tag, &ok, deadline), CompletionQueue::GOT_EVENT);
  EXPECT_EQ(got_tag, tag(3));
  EXPECT_EQ(status.error_code(), StatusCode::UNIMPLEMENTED);
}

TEST_F(CardinalityViolationTest, ServerStreamingZeroRequests) {
  const char* experiments = std::getenv("GRPC_EXPERIMENTS");
  if (experiments != nullptr &&
      absl::StrContains(experiments, "promise_based_http2_client_transport")) {
    GTEST_SKIP() << "Skipping due to promise based transport limitation";
  }
  ClientContext context;
  Status status;
  GenericStub generic_stub(channel_);
  CompletionQueue cq;
  std::unique_ptr<GenericClientAsyncReaderWriter> call =
      generic_stub.PrepareCall(
          &context, "/grpc.testing.EchoTestService/ResponseStream", &cq);
  call->StartCall(tag(1));
  void* got_tag;
  bool ok;
  gpr_timespec deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                       gpr_time_from_seconds(10, GPR_TIMESPAN));
  EXPECT_EQ(cq.AsyncNext(&got_tag, &ok, deadline), CompletionQueue::GOT_EVENT);
  EXPECT_EQ(got_tag, tag(1));
  call->WritesDone(tag(2));
  deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                          gpr_time_from_seconds(10, GPR_TIMESPAN));
  EXPECT_EQ(cq.AsyncNext(&got_tag, &ok, deadline), CompletionQueue::GOT_EVENT);
  EXPECT_EQ(got_tag, tag(2));
  call->Finish(&status, tag(3));
  deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                          gpr_time_from_seconds(10, GPR_TIMESPAN));
  EXPECT_EQ(cq.AsyncNext(&got_tag, &ok, deadline), CompletionQueue::GOT_EVENT);
  EXPECT_EQ(got_tag, tag(3));
  EXPECT_EQ(status.error_code(), StatusCode::UNIMPLEMENTED);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
