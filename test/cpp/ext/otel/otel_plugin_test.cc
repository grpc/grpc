//
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
//

#include "src/cpp/ext/otel/otel_plugin.h"

#include "absl/functional/any_invocable.h"
#include "api/include/opentelemetry/metrics/provider.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"

#include <grpcpp/grpcpp.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/config/core_configuration.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {
namespace {

TEST(OTelPluginBuildTest, ApiDependency) {
  opentelemetry::metrics::Provider::GetMeterProvider();
}

TEST(OTelPluginBuildTest, SdkDependency) {
  opentelemetry::sdk::metrics::MeterProvider();
}

class MockMetricReader : public opentelemetry::sdk::metrics::MetricReader {
 public:
  opentelemetry::sdk::metrics::AggregationTemporality GetAggregationTemporality(
      opentelemetry::sdk::metrics::InstrumentType) const noexcept override {
    return opentelemetry::sdk::metrics::AggregationTemporality::kDelta;
  }

  bool OnForceFlush(std::chrono::microseconds) noexcept override {
    return true;
  }

  bool OnShutDown(std::chrono::microseconds) noexcept override { return true; }

  void OnInitialized() noexcept override {}
};

class OTelPluginEnd2EndTest : public ::testing::Test {
 protected:
  using ::testing::Test::SetUp;
  void SetUp(const absl::flat_hash_set<absl::string_view>& metric_names,
             bool global_meter_provider = false) {
    // We are resetting the MeterProvider and OpenTelemetry plugin at the start
    // of each test to avoid test results from one test carrying over to another
    // test. (Some measurements can get arbitrarily delayed.)
    auto meter_provider =
        std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
    reader_.reset(new grpc::testing::MockMetricReader);
    meter_provider->AddMetricReader(reader_);
    grpc_core::CoreConfiguration::Reset();
    grpc::internal::OpenTelemetryPluginBuilder ot_builder;
    ot_builder.EnableMetrics(metric_names);
    if (global_meter_provider) {
      opentelemetry::metrics::Provider::SetMeterProvider(
          opentelemetry::nostd::shared_ptr<
              opentelemetry::metrics::MeterProvider>(
              std::move(meter_provider)));
    } else {
      ot_builder.SetMeterProvider(std::move(meter_provider));
    }
    ot_builder.BuildAndRegisterGlobal();
    grpc_init();
    grpc::ServerBuilder builder;
    int port;
    // Use IPv4 here because it's less flaky than IPv6 ("[::]:0") on Travis.
    builder.AddListeningPort("0.0.0.0:0", grpc::InsecureServerCredentials(),
                             &port);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(nullptr, server_);
    ASSERT_NE(0, port);
    server_address_ = absl::StrCat("localhost:", port);

    stub_ = EchoTestService::NewStub(grpc::CreateChannel(
        server_address_, grpc::InsecureChannelCredentials()));
  }

  void TearDown() override {
    server_->Shutdown();
    grpc_shutdown_blocking();
    delete grpc_core::ServerCallTracerFactory::Get(grpc_core::ChannelArgs());
    grpc_core::ServerCallTracerFactory::RegisterGlobal(nullptr);
  }

  void ResetStub(std::shared_ptr<Channel> channel) {
    stub_ = EchoTestService::NewStub(channel);
  }

  void SendRPC() {
    EchoRequest request;
    request.set_message("foo");
    EchoResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->Echo(&context, request, &response);
  }

  absl::flat_hash_map<std::string,
                      std::vector<opentelemetry::sdk::metrics::PointType>>
  ReadCurrentMetricsData(
      absl::AnyInvocable<
          bool(const absl::flat_hash_map<
               std::string,
               std::vector<opentelemetry::sdk::metrics::PointType>>&)>
          continue_predicate) {
    absl::flat_hash_map<std::string,
                        std::vector<opentelemetry::sdk::metrics::PointType>>
        data;
    auto deadline = absl::Now() + absl::Seconds(5);
    do {
      reader_->Collect([&](opentelemetry::sdk::metrics::ResourceMetrics& rm) {
        for (const opentelemetry::sdk::metrics::ScopeMetrics& smd :
             rm.scope_metric_data_) {
          for (const opentelemetry::sdk::metrics::MetricData& md :
               smd.metric_data_) {
            for (const opentelemetry::sdk::metrics::PointDataAttributes& dp :
                 md.point_data_attr_) {
              data[md.instrument_descriptor.name_].push_back(dp.point_data);
            }
          }
        }
        return true;
      });
    } while (continue_predicate(data) && deadline > absl::Now());
    return data;
  }

  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader_;
  std::string server_address_;
  CallbackTestServiceImpl service_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<EchoTestService::Stub> stub_;
};

TEST_F(OTelPluginEnd2EndTest, ClientAttemptStarted) {
  SetUp({grpc::internal::OTelClientAttemptStartedInstrumentName()});
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string, std::vector<opentelemetry::sdk::metrics::PointType>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0]);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  ASSERT_EQ(*client_started_value, 1);
}

TEST_F(OTelPluginEnd2EndTest, ClientAttemptDuration) {
  SetUp({grpc::internal::OTelClientAttemptDurationInstrumentName()});
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string, std::vector<opentelemetry::sdk::metrics::PointType>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0]);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
}

TEST_F(OTelPluginEnd2EndTest, ClientAttemptSentTotalCompressedMessageSize) {
  SetUp({grpc::internal::
             OTelClientAttemptSentTotalCompressedMessageSizeInstrumentName()});
  SendRPC();
  const char* kMetricName =
      "grpc.client.attempt.sent_total_compressed_message_size";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string, std::vector<opentelemetry::sdk::metrics::PointType>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0]);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
}

TEST_F(OTelPluginEnd2EndTest, ClientAttemptRcvdTotalCompressedMessageSize) {
  SetUp({grpc::internal::
             OTelClientAttemptRcvdTotalCompressedMessageSizeInstrumentName()});
  SendRPC();
  const char* kMetricName =
      "grpc.client.attempt.rcvd_total_compressed_message_size";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string, std::vector<opentelemetry::sdk::metrics::PointType>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0]);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
}

TEST_F(OTelPluginEnd2EndTest, ServerCallStarted) {
  SetUp({grpc::internal::OTelServerCallStartedInstrumentName()});
  SendRPC();
  const char* kMetricName = "grpc.server.call.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string, std::vector<opentelemetry::sdk::metrics::PointType>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0]);
  ASSERT_NE(point_data, nullptr);
  auto server_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(server_started_value, nullptr);
  ASSERT_EQ(*server_started_value, 1);
}

TEST_F(OTelPluginEnd2EndTest, ServerCallDuration) {
  SetUp({grpc::internal::OTelServerCallDurationInstrumentName()});
  SendRPC();
  const char* kMetricName = "grpc.server.call.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string, std::vector<opentelemetry::sdk::metrics::PointType>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0]);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
}

TEST_F(OTelPluginEnd2EndTest, ServerCallSentTotalCompressedMessageSize) {
  SetUp({grpc::internal::
             OTelServerCallSentTotalCompressedMessageSizeInstrumentName()});
  SendRPC();
  const char* kMetricName =
      "grpc.server.call.sent_total_compressed_message_size";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string, std::vector<opentelemetry::sdk::metrics::PointType>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0]);
  ASSERT_NE(point_data, nullptr);
  EXPECT_EQ(point_data->count_, 1);
}

TEST_F(OTelPluginEnd2EndTest, ServerCallRcvdTotalCompressedMessageSize) {
  SetUp({grpc::internal::
             OTelServerCallRcvdTotalCompressedMessageSizeInstrumentName()});
  SendRPC();
  const char* kMetricName =
      "grpc.server.call.rcvd_total_compressed_message_size";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string, std::vector<opentelemetry::sdk::metrics::PointType>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0]);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
}

// Make sure that things work with the global meter provider as well
TEST_F(OTelPluginEnd2EndTest, UseGlobalMeterProvider) {
  SetUp({grpc::internal::OTelClientAttemptStartedInstrumentName()});
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string, std::vector<opentelemetry::sdk::metrics::PointType>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0]);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  ASSERT_EQ(*client_started_value, 1);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
