//
//
// Copyright 2018 gRPC authors.
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

#include <grpc++/grpc++.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpcpp/ext/server_load_reporting.h>
#include <grpcpp/server_builder.h>

#include <thread>

#include "absl/log/log.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/client_channel/backup_poller.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/crash.h"
#include "src/proto/grpc/lb/v1/load_reporter.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace testing {
namespace {

constexpr double kMetricValue = 3.1415;
constexpr char kMetricName[] = "METRIC_PI";

// Different messages result in different response statuses. For simplicity in
// computing request bytes, the message sizes should be the same.
const char kOkMessage[] = "hello";
const char kServerErrorMessage[] = "sverr";
const char kClientErrorMessage[] = "clerr";

class EchoTestServiceImpl : public EchoTestService::Service {
 public:
  ~EchoTestServiceImpl() override {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    if (request->message() == kServerErrorMessage) {
      return Status(StatusCode::UNKNOWN, "Server error requested");
    }
    if (request->message() == kClientErrorMessage) {
      return Status(StatusCode::FAILED_PRECONDITION, "Client error requested");
    }
    response->set_message(request->message());
    grpc::load_reporter::experimental::AddLoadReportingCost(
        context, kMetricName, kMetricValue);
    return Status::OK;
  }
};

class ServerLoadReportingEnd2endTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_address_ =
        "localhost:" + std::to_string(grpc_pick_unused_port_or_die());
    server_ =
        ServerBuilder()
            .AddListeningPort(server_address_, InsecureServerCredentials())
            .RegisterService(&echo_service_)
            .SetOption(std::unique_ptr<grpc::ServerBuilderOption>(
                new grpc::load_reporter::experimental::
                    LoadReportingServiceServerBuilderOption()))
            .BuildAndStart();
    server_thread_ =
        std::thread(&ServerLoadReportingEnd2endTest::RunServerLoop, this);
  }

  void RunServerLoop() { server_->Wait(); }

  void TearDown() override {
    server_->Shutdown();
    server_thread_.join();
  }

  void ClientMakeEchoCalls(const std::string& lb_id, const std::string& lb_tag,
                           const std::string& message, size_t num_requests) {
    auto stub = EchoTestService::NewStub(
        grpc::CreateChannel(server_address_, InsecureChannelCredentials()));
    std::string lb_token = lb_id + lb_tag;
    for (size_t i = 0; i < num_requests; ++i) {
      ClientContext ctx;
      if (!lb_token.empty()) ctx.AddMetadata(GRPC_LB_TOKEN_MD_KEY, lb_token);
      EchoRequest request;
      EchoResponse response;
      request.set_message(message);
      Status status = stub->Echo(&ctx, request, &response);
      if (message == kOkMessage) {
        ASSERT_EQ(status.error_code(), StatusCode::OK);
        ASSERT_EQ(request.message(), response.message());
      } else if (message == kServerErrorMessage) {
        ASSERT_EQ(status.error_code(), StatusCode::UNKNOWN);
      } else if (message == kClientErrorMessage) {
        ASSERT_EQ(status.error_code(), StatusCode::FAILED_PRECONDITION);
      }
    }
  }

  std::string server_address_;
  std::unique_ptr<Server> server_;
  std::thread server_thread_;
  EchoTestServiceImpl echo_service_;
};

TEST_F(ServerLoadReportingEnd2endTest, NoCall) {}

TEST_F(ServerLoadReportingEnd2endTest, BasicReport) {
  auto channel =
      grpc::CreateChannel(server_address_, InsecureChannelCredentials());
  auto stub = grpc::lb::v1::LoadReporter::NewStub(channel);
  ClientContext ctx;
  auto stream = stub->ReportLoad(&ctx);
  grpc::lb::v1::LoadReportRequest request;
  request.mutable_initial_request()->set_load_balanced_hostname(
      server_address_);
  request.mutable_initial_request()->set_load_key("LOAD_KEY");
  request.mutable_initial_request()
      ->mutable_load_report_interval()
      ->set_seconds(5);
  stream->Write(request);
  LOG(INFO) << "Initial request sent.";
  grpc::lb::v1::LoadReportResponse response;
  stream->Read(&response);
  const std::string& lb_id = response.initial_response().load_balancer_id();
  LOG(INFO) << "Initial response received (lb_id: " << lb_id << ").";
  ClientMakeEchoCalls(lb_id, "LB_TAG", kOkMessage, 1);

  unsigned load_count = 0;
  bool got_in_progress = false;
  bool got_orphaned = false;
  bool got_calls = false;
  while (load_count < 3) {
    stream->Read(&response);
    for (const auto& load : response.load()) {
      load_count++;
      if (load.in_progress_report_case()) {
        // The special load record that reports the number of in-progress
        // calls.
        EXPECT_EQ(load.num_calls_in_progress(), 1);
        EXPECT_FALSE(got_in_progress);
        got_in_progress = true;
      } else if (load.orphaned_load_case()) {
        // The call from the balancer doesn't have any valid LB token.
        EXPECT_EQ(load.orphaned_load_case(), load.kLoadKeyUnknown);
        EXPECT_EQ(load.num_calls_started(), 1);
        EXPECT_EQ(load.num_calls_finished_without_error(), 0);
        EXPECT_EQ(load.num_calls_finished_with_error(), 0);
        EXPECT_FALSE(got_orphaned);
        got_orphaned = true;
      } else {
        // This corresponds to the calls from the client.
        EXPECT_EQ(load.num_calls_started(), 1);
        EXPECT_EQ(load.num_calls_finished_without_error(), 1);
        EXPECT_EQ(load.num_calls_finished_with_error(), 0);
        EXPECT_GE(load.total_bytes_received(), sizeof(kOkMessage));
        EXPECT_GE(load.total_bytes_sent(), sizeof(kOkMessage));
        EXPECT_EQ(load.metric_data().size(), 1);
        EXPECT_EQ(load.metric_data().Get(0).metric_name(), kMetricName);
        EXPECT_EQ(load.metric_data().Get(0).num_calls_finished_with_metric(),
                  1);
        EXPECT_EQ(load.metric_data().Get(0).total_metric_value(), kMetricValue);
        EXPECT_FALSE(got_calls);
        got_calls = true;
      }
    }
  }
  EXPECT_EQ(load_count, 3);
  EXPECT_TRUE(got_in_progress);
  EXPECT_TRUE(got_orphaned);
  EXPECT_TRUE(got_calls);
  stream->WritesDone();
  EXPECT_EQ(stream->Finish().error_code(), StatusCode::CANCELLED);
}

// TODO(juanlishen): Add more tests.

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  grpc_core::ConfigVars::Overrides config_overrides;
  config_overrides.client_channel_backup_poll_interval_ms = 1;
  grpc_core::ConfigVars::SetOverrides(config_overrides);
  return RUN_ALL_TESTS();
}
