/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc++/grpc++.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpcpp/ext/server_load_reporting.h>
#include <grpcpp/server_builder.h>

#include "src/proto/grpc/lb/v1/load_reporter.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

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
    ::grpc::load_reporter::experimental::AddLoadReportingCost(
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
            .SetOption(std::unique_ptr<::grpc::ServerBuilderOption>(
                new ::grpc::load_reporter::experimental::
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
  auto stub = ::grpc::lb::v1::LoadReporter::NewStub(channel);
  ClientContext ctx;
  auto stream = stub->ReportLoad(&ctx);
  ::grpc::lb::v1::LoadReportRequest request;
  request.mutable_initial_request()->set_load_balanced_hostname(
      server_address_);
  request.mutable_initial_request()->set_load_key("LOAD_KEY");
  request.mutable_initial_request()
      ->mutable_load_report_interval()
      ->set_seconds(5);
  stream->Write(request);
  gpr_log(GPR_INFO, "Initial request sent.");
  ::grpc::lb::v1::LoadReportResponse response;
  stream->Read(&response);
  const std::string& lb_id = response.initial_response().load_balancer_id();
  gpr_log(GPR_INFO, "Initial response received (lb_id: %s).", lb_id.c_str());
  ClientMakeEchoCalls(lb_id, "LB_TAG", kOkMessage, 1);
  while (true) {
    stream->Read(&response);
    if (!response.load().empty()) {
      ASSERT_EQ(response.load().size(), 3);
      for (const auto& load : response.load()) {
        if (load.in_progress_report_case()) {
          // The special load record that reports the number of in-progress
          // calls.
          ASSERT_EQ(load.num_calls_in_progress(), 1);
        } else if (load.orphaned_load_case()) {
          // The call from the balancer doesn't have any valid LB token.
          ASSERT_EQ(load.orphaned_load_case(), load.kLoadKeyUnknown);
          ASSERT_EQ(load.num_calls_started(), 1);
          ASSERT_EQ(load.num_calls_finished_without_error(), 0);
          ASSERT_EQ(load.num_calls_finished_with_error(), 0);
        } else {
          // This corresponds to the calls from the client.
          ASSERT_EQ(load.num_calls_started(), 1);
          ASSERT_EQ(load.num_calls_finished_without_error(), 1);
          ASSERT_EQ(load.num_calls_finished_with_error(), 0);
          ASSERT_GE(load.total_bytes_received(), sizeof(kOkMessage));
          ASSERT_GE(load.total_bytes_sent(), sizeof(kOkMessage));
          ASSERT_EQ(load.metric_data().size(), 1);
          ASSERT_EQ(load.metric_data().Get(0).metric_name(), kMetricName);
          ASSERT_EQ(load.metric_data().Get(0).num_calls_finished_with_metric(),
                    1);
          ASSERT_EQ(load.metric_data().Get(0).total_metric_value(),
                    kMetricValue);
        }
      }
      break;
    }
  }
  stream->WritesDone();
  ASSERT_EQ(stream->Finish().error_code(), StatusCode::CANCELLED);
}

// TODO(juanlishen): Add more tests.

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
