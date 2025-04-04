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

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/call_metric_recorder.h>
#include <grpcpp/ext/orca_service.h>
#include <grpcpp/ext/server_metric_recorder.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/byte_buffer.h>
#include <grpcpp/support/status.h>

#include <memory>
#include <optional>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/util/notification.h"
#include "src/core/util/time.h"
#include "src/proto/grpc/testing/xds/v3/orca_service.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/orca_service.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

using xds::data::orca::v3::OrcaLoadReport;
using xds::service::orca::v3::OpenRcaService;
using xds::service::orca::v3::OrcaLoadReportRequest;

namespace grpc {
namespace testing {
namespace {

using experimental::OrcaService;
using experimental::ServerMetricRecorder;

class OrcaServiceEnd2endTest : public ::testing::Test {
 protected:
  // A wrapper for the client stream that ensures that responses come
  // back at the requested interval.
  class Stream {
   public:
    Stream(OpenRcaService::Stub* stub, grpc_core::Duration requested_interval)
        : requested_interval_(requested_interval) {
      OrcaLoadReportRequest request;
      gpr_timespec timespec = requested_interval.as_timespec();
      auto* interval_proto = request.mutable_report_interval();
      interval_proto->set_seconds(timespec.tv_sec);
      interval_proto->set_nanos(timespec.tv_nsec);
      stream_ = stub->StreamCoreMetrics(&context_, request);
    }

    ~Stream() { context_.TryCancel(); }

    OrcaLoadReport ReadResponse() {
      OrcaLoadReport response;
      EXPECT_TRUE(stream_->Read(&response));
      auto now = grpc_core::Timestamp::FromTimespecRoundDown(
          gpr_now(GPR_CLOCK_MONOTONIC));
      if (last_response_time_.has_value()) {
        // Allow a small fudge factor to avoid test flakiness.
        const grpc_core::Duration fudge_factor =
            grpc_core::Duration::Milliseconds(750) *
            grpc_test_slowdown_factor();
        auto elapsed = now - *last_response_time_;
        LOG(INFO) << "received ORCA response after " << elapsed;
        EXPECT_GE(elapsed, requested_interval_ - fudge_factor)
            << elapsed.ToString();
        EXPECT_LE(elapsed, requested_interval_ + fudge_factor)
            << elapsed.ToString();
      }
      last_response_time_ = now;
      return response;
    }

   private:
    const grpc_core::Duration requested_interval_;
    ClientContext context_;
    std::unique_ptr<grpc::ClientReaderInterface<OrcaLoadReport>> stream_;
    std::optional<grpc_core::Timestamp> last_response_time_;
  };

  class GenericOrcaClientReactor
      : public grpc::ClientBidiReactor<grpc::ByteBuffer, grpc::ByteBuffer> {
   public:
    explicit GenericOrcaClientReactor(GenericStub* stub) : stub_(stub) {}

    void Prepare() {
      stub_->PrepareBidiStreamingCall(
          &cli_ctx_, "/xds.service.orca.v3.OpenRcaService/StreamCoreMetrics",
          StubOptions(), this);
    }

    grpc::Status Await() {
      notification_.WaitForNotification();
      return status_;
    }

    void OnDone(const grpc::Status& s) override {
      status_ = s;
      notification_.Notify();
    }

   private:
    GenericStub* stub_;
    grpc::ClientContext cli_ctx_;
    grpc_core::Notification notification_;
    grpc::Status status_;
  };

  OrcaServiceEnd2endTest()
      : server_metric_recorder_(ServerMetricRecorder::Create()),
        orca_service_(server_metric_recorder_.get(),
                      OrcaService::Options().set_min_report_duration(
                          absl::ZeroDuration())) {
    std::string server_address =
        absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
    ServerBuilder builder;
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    builder.RegisterService(&orca_service_);
    server_ = builder.BuildAndStart();
    LOG(INFO) << "server started on " << server_address;
    channel_ = CreateChannel(server_address, InsecureChannelCredentials());
  }

  ~OrcaServiceEnd2endTest() override { server_->Shutdown(); }

  std::unique_ptr<ServerMetricRecorder> server_metric_recorder_;
  OrcaService orca_service_;
  std::unique_ptr<Server> server_;
  std::shared_ptr<Channel> channel_;
};

TEST_F(OrcaServiceEnd2endTest, Basic) {
  constexpr char kMetricName1[] = "foo";
  constexpr char kMetricName2[] = "bar";
  constexpr char kMetricName3[] = "baz";
  constexpr char kMetricName4[] = "quux";
  auto stub = OpenRcaService::NewStub(channel_);
  // Start stream1 with 5s interval and stream2 with 2.5s interval.
  // Throughout the test, we should get two responses on stream2 for
  // every one response on stream1.
  Stream stream1(stub.get(), grpc_core::Duration::Milliseconds(5000));
  Stream stream2(stub.get(), grpc_core::Duration::Milliseconds(2500));
  auto ReadResponses = [&](std::function<void(const OrcaLoadReport&)> checker) {
    LOG(INFO) << "reading response from stream1";
    OrcaLoadReport response = stream1.ReadResponse();
    checker(response);
    LOG(INFO) << "reading response from stream2";
    response = stream2.ReadResponse();
    checker(response);
    LOG(INFO) << "reading response from stream2";
    response = stream2.ReadResponse();
    checker(response);
  };
  // Initial response should not have any values populated.
  ReadResponses([](const OrcaLoadReport& response) {
    EXPECT_EQ(response.application_utilization(), 0);
    EXPECT_EQ(response.cpu_utilization(), 0);
    EXPECT_EQ(response.mem_utilization(), 0);
    EXPECT_THAT(response.utilization(), ::testing::UnorderedElementsAre());
  });
  // Now set app utilization on the server.
  server_metric_recorder_->SetApplicationUtilization(0.5);
  ReadResponses([](const OrcaLoadReport& response) {
    EXPECT_EQ(response.application_utilization(), 0.5);
    EXPECT_EQ(response.cpu_utilization(), 0);
    EXPECT_EQ(response.mem_utilization(), 0);
    EXPECT_THAT(response.utilization(), ::testing::UnorderedElementsAre());
  });
  // Update app utilization and set CPU and memory utilization.
  server_metric_recorder_->SetApplicationUtilization(1.8);
  server_metric_recorder_->SetCpuUtilization(0.3);
  server_metric_recorder_->SetMemoryUtilization(0.4);
  ReadResponses([](const OrcaLoadReport& response) {
    EXPECT_EQ(response.application_utilization(), 1.8);
    EXPECT_EQ(response.cpu_utilization(), 0.3);
    EXPECT_EQ(response.mem_utilization(), 0.4);
    EXPECT_THAT(response.utilization(), ::testing::UnorderedElementsAre());
  });
  // Unset app, CPU, and memory utilization and set a named utilization.
  server_metric_recorder_->ClearApplicationUtilization();
  server_metric_recorder_->ClearCpuUtilization();
  server_metric_recorder_->ClearMemoryUtilization();
  server_metric_recorder_->SetNamedUtilization(kMetricName1, 0.3);
  ReadResponses([&](const OrcaLoadReport& response) {
    EXPECT_EQ(response.application_utilization(), 0);
    EXPECT_EQ(response.cpu_utilization(), 0);
    EXPECT_EQ(response.mem_utilization(), 0);
    EXPECT_THAT(
        response.utilization(),
        ::testing::UnorderedElementsAre(::testing::Pair(kMetricName1, 0.3)));
  });
  // Unset the previous named utilization and set two new ones.
  server_metric_recorder_->ClearNamedUtilization(kMetricName1);
  server_metric_recorder_->SetNamedUtilization(kMetricName2, 0.2);
  server_metric_recorder_->SetNamedUtilization(kMetricName3, 0.1);
  ReadResponses([&](const OrcaLoadReport& response) {
    EXPECT_EQ(response.application_utilization(), 0);
    EXPECT_EQ(response.cpu_utilization(), 0);
    EXPECT_EQ(response.mem_utilization(), 0);
    EXPECT_THAT(
        response.utilization(),
        ::testing::UnorderedElementsAre(::testing::Pair(kMetricName2, 0.2),
                                        ::testing::Pair(kMetricName3, 0.1)));
  });
  // Replace the entire named metric map at once.
  server_metric_recorder_->SetAllNamedUtilization(
      {{kMetricName2, 0.5}, {kMetricName4, 0.9}});
  ReadResponses([&](const OrcaLoadReport& response) {
    EXPECT_EQ(response.application_utilization(), 0);
    EXPECT_EQ(response.cpu_utilization(), 0);
    EXPECT_EQ(response.mem_utilization(), 0);
    EXPECT_THAT(
        response.utilization(),
        ::testing::UnorderedElementsAre(::testing::Pair(kMetricName2, 0.5),
                                        ::testing::Pair(kMetricName4, 0.9)));
  });
}

TEST_F(OrcaServiceEnd2endTest, ClientClosesBeforeSendingMessage) {
  auto stub = std::make_unique<GenericStub>(channel_);
  GenericOrcaClientReactor reactor(stub.get());
  reactor.Prepare();
  reactor.StartWritesDone();
  reactor.StartCall();
  EXPECT_EQ(reactor.Await().error_code(), grpc::StatusCode::INTERNAL);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
