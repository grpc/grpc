//
//
// Copyright 2022 gRPC authors.
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

#include <thread>  // NOLINT

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

#include <grpc++/grpc++.h>

#include "src/cpp/ext/filters/logging/logging_filter.h"
#include "src/cpp/ext/gcp/observability_logging_sink.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {

namespace {

class MyTestServiceImpl : public TestServiceImpl {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    EchoRequest new_request;
    new_request.set_message("hello");
    return TestServiceImpl::Echo(context, &new_request, response);
  }
};

class TestLoggingSink : public grpc::internal::LoggingSink {
 public:
  Config FindMatch(bool /* is_client */, absl::string_view /* service */,
                   absl::string_view /* method */) override {
    return Config(4096, 4096);
  }

  void LogEntry(Entry entry) override {
    ::google::protobuf::Struct json;
    grpc::internal::EntryToJsonStructProto(entry, &json);
    std::string output;
    ::google::protobuf::TextFormat::PrintToString(json, &output);
    gpr_log(GPR_ERROR, "entry %s", output.c_str());
  }
};

TestLoggingSink* g_test_logging_sink = nullptr;

class LoggingTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    g_test_logging_sink = new TestLoggingSink;
    ::grpc::internal::RegisterLoggingFilter(g_test_logging_sink);
  }

  void SetUp() override {
    // Set up a synchronous server on a different thread to avoid the asynch
    // interface.
    grpc::ServerBuilder builder;
    int port = grpc_pick_unused_port_or_die();
    server_address_ = absl::StrCat("localhost:", port);
    // Use IPv4 here because it's less flaky than IPv6 ("[::]:0") on Travis.
    builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials(),
                             &port);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(nullptr, server_);

    server_thread_ = std::thread(&LoggingTest::RunServerLoop, this);

    stub_ = EchoTestService::NewStub(grpc::CreateChannel(
        server_address_, grpc::InsecureChannelCredentials()));
  }

  void ResetStub(std::shared_ptr<Channel> channel) {
    stub_ = EchoTestService::NewStub(channel);
  }

  void TearDown() override {
    server_->Shutdown();
    server_thread_.join();
  }

  void RunServerLoop() { server_->Wait(); }

  std::string server_address_;
  MyTestServiceImpl service_;
  std::unique_ptr<grpc::Server> server_;
  std::thread server_thread_;

  std::unique_ptr<EchoTestService::Stub> stub_;
};

TEST_F(LoggingTest, SimpleRpc) {
  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
