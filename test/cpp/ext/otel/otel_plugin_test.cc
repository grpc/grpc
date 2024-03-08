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
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/metrics/data/point_data.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"

#include <grpcpp/ext/otel_plugin.h>
#include <grpcpp/grpcpp.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/config/core_configuration.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/ext/otel/otel_test_library.h"

namespace grpc {
namespace testing {
namespace {

#define GRPC_ARG_SERVER_SELECTOR_KEY "grpc.testing.server_selector_key"
#define GRPC_ARG_SERVER_SELECTOR_VALUE "grpc.testing.server_selector_value"

TEST(OpenTelemetryPluginBuildTest, ApiDependency) {
  opentelemetry::metrics::Provider::GetMeterProvider();
}

TEST(OpenTelemetryPluginBuildTest, SdkDependency) {
  opentelemetry::sdk::metrics::MeterProvider();
}

TEST(OpenTelemetryPluginBuildTest, Basic) {
  grpc::OpenTelemetryPluginBuilder builder;
}

TEST_F(OpenTelemetryPluginEnd2EndTest, ClientAttemptStarted) {
  Init(std::move(
      Options().set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                      kClientAttemptStartedInstrumentName})));
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      absl::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
}

TEST_F(OpenTelemetryPluginEnd2EndTest, ClientAttemptDuration) {
  Init(std::move(
      Options().set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                      kClientAttemptDurationInstrumentName})));
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 3);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      absl::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
  const auto* status_value =
      absl::get_if<std::string>(&attributes.at("grpc.status"));
  ASSERT_NE(status_value, nullptr);
  EXPECT_EQ(*status_value, "OK");
}

TEST_F(OpenTelemetryPluginEnd2EndTest,
       ClientAttemptSentTotalCompressedMessageSize) {
  Init(std::move(Options().set_metric_names(
      {grpc::OpenTelemetryPluginBuilder::
           kClientAttemptSentTotalCompressedMessageSizeInstrumentName})));
  SendRPC();
  const char* kMetricName =
      "grpc.client.attempt.sent_total_compressed_message_size";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  ASSERT_EQ(absl::get<int64_t>(point_data->max_), 5);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 3);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      absl::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
  const auto* status_value =
      absl::get_if<std::string>(&attributes.at("grpc.status"));
  ASSERT_NE(status_value, nullptr);
  EXPECT_EQ(*status_value, "OK");
}

TEST_F(OpenTelemetryPluginEnd2EndTest,
       ClientAttemptRcvdTotalCompressedMessageSize) {
  Init(std::move(Options().set_metric_names(
      {grpc::OpenTelemetryPluginBuilder::
           kClientAttemptRcvdTotalCompressedMessageSizeInstrumentName})));
  SendRPC();
  const char* kMetricName =
      "grpc.client.attempt.rcvd_total_compressed_message_size";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  ASSERT_EQ(absl::get<int64_t>(point_data->max_), 5);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 3);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      absl::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
  const auto* status_value =
      absl::get_if<std::string>(&attributes.at("grpc.status"));
  ASSERT_NE(status_value, nullptr);
  EXPECT_EQ(*status_value, "OK");
}

TEST_F(OpenTelemetryPluginEnd2EndTest, ServerCallStarted) {
  Init(std::move(Options().set_metric_names(
      {grpc::OpenTelemetryPluginBuilder::kServerCallStartedInstrumentName})));
  SendRPC();
  const char* kMetricName = "grpc.server.call.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto server_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(server_started_value, nullptr);
  ASSERT_EQ(*server_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 1);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
}

TEST_F(OpenTelemetryPluginEnd2EndTest, ServerCallDuration) {
  Init(std::move(Options().set_metric_names(
      {grpc::OpenTelemetryPluginBuilder::kServerCallDurationInstrumentName})));
  SendRPC();
  const char* kMetricName = "grpc.server.call.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* status_value =
      absl::get_if<std::string>(&attributes.at("grpc.status"));
  ASSERT_NE(status_value, nullptr);
  EXPECT_EQ(*status_value, "OK");
}

TEST_F(OpenTelemetryPluginEnd2EndTest,
       ServerCallSentTotalCompressedMessageSize) {
  Init(std::move(Options().set_metric_names(
      {grpc::OpenTelemetryPluginBuilder::
           kServerCallSentTotalCompressedMessageSizeInstrumentName})));
  SendRPC();
  const char* kMetricName =
      "grpc.server.call.sent_total_compressed_message_size";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  EXPECT_EQ(point_data->count_, 1);
  ASSERT_EQ(absl::get<int64_t>(point_data->max_), 5);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* status_value =
      absl::get_if<std::string>(&attributes.at("grpc.status"));
  ASSERT_NE(status_value, nullptr);
  EXPECT_EQ(*status_value, "OK");
}

TEST_F(OpenTelemetryPluginEnd2EndTest,
       ServerCallRcvdTotalCompressedMessageSize) {
  Init(std::move(Options().set_metric_names(
      {grpc::OpenTelemetryPluginBuilder::
           kServerCallRcvdTotalCompressedMessageSizeInstrumentName})));
  SendRPC();
  const char* kMetricName =
      "grpc.server.call.rcvd_total_compressed_message_size";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  ASSERT_EQ(absl::get<int64_t>(point_data->max_), 5);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* status_value =
      absl::get_if<std::string>(&attributes.at("grpc.status"));
  ASSERT_NE(status_value, nullptr);
  EXPECT_EQ(*status_value, "OK");
}

// Make sure that no meter provider results in normal operations.
TEST_F(OpenTelemetryPluginEnd2EndTest, NoMeterProviderRegistered) {
  Init(
      std::move(Options()
                    .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptStartedInstrumentName})
                    .set_use_meter_provider(false)));
  SendRPC();
}

// Test that a channel selector returning true records metrics on the channel.
TEST_F(OpenTelemetryPluginEnd2EndTest, TargetSelectorReturnsTrue) {
  Init(
      std::move(Options()
                    .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptStartedInstrumentName})
                    .set_target_selector(
                        [](absl::string_view /*target*/) { return true; })));
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      absl::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
}

// Test that a target selector returning false does not record metrics on the
// channel.
TEST_F(OpenTelemetryPluginEnd2EndTest, TargetSelectorReturnsFalse) {
  Init(
      std::move(Options()
                    .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptStartedInstrumentName})
                    .set_target_selector(
                        [](absl::string_view /*target*/) { return false; })));
  SendRPC();
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
          /*data*/) { return false; });
  ASSERT_TRUE(data.empty());
}

// Test that a server selector returning true records metrics on the server.
TEST_F(OpenTelemetryPluginEnd2EndTest, ServerSelectorReturnsTrue) {
  Init(std::move(Options()
                     .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                            kServerCallDurationInstrumentName})
                     .set_server_selector(
                         [](const grpc_core::ChannelArgs& /*channel_args*/) {
                           return true;
                         })));
  SendRPC();
  const char* kMetricName = "grpc.server.call.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  const auto& server_attributes =
      data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(server_attributes.size(), 2);
  EXPECT_EQ(absl::get<std::string>(server_attributes.at("grpc.method")),
            kMethodName);
  EXPECT_EQ(absl::get<std::string>(server_attributes.at("grpc.status")), "OK");
}

// Test that a server selector returning false does not record metrics on the
// server.
TEST_F(OpenTelemetryPluginEnd2EndTest, ServerSelectorReturnsFalse) {
  Init(std::move(Options()
                     .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                            kServerCallDurationInstrumentName})
                     .set_server_selector(
                         [](const grpc_core::ChannelArgs& /*channel_args*/) {
                           return false;
                         })));
  SendRPC();
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
          /*data*/) { return false; });
  ASSERT_TRUE(data.empty());
}

// Test that a target attribute filter returning true records metrics with the
// target as is on the channel.
TEST_F(OpenTelemetryPluginEnd2EndTest, TargetAttributeFilterReturnsTrue) {
  Init(
      std::move(Options()
                    .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptStartedInstrumentName})
                    .set_target_attribute_filter(
                        [](absl::string_view /*target*/) { return true; })));
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      absl::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
}

// Test that a target attribute filter returning false records metrics with the
// target as "other".
TEST_F(OpenTelemetryPluginEnd2EndTest, TargetAttributeFilterReturnsFalse) {
  Init(
      std::move(Options()
                    .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptStartedInstrumentName})
                    .set_target_attribute_filter(
                        [](absl::string_view /*target*/) { return false; })));
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
          /*data*/) { return false; });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      absl::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, "other");
}

// Test that generic method names are scrubbed properly on the client side.
TEST_F(OpenTelemetryPluginEnd2EndTest, GenericClientRpc) {
  Init(std::move(
      Options().set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                      kClientAttemptStartedInstrumentName})));
  SendGenericRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
          /*data*/) { return false; });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, "other");
  const auto* target_value =
      absl::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
}

// Test that generic method names are scrubbed properly on the client side if
// the method attribute filter is set and it returns false.
TEST_F(OpenTelemetryPluginEnd2EndTest,
       GenericClientRpcWithMethodAttributeFilterReturningFalse) {
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kClientAttemptStartedInstrumentName})
          .set_generic_method_attribute_filter(
              [](absl::string_view /*generic_method*/) { return false; })));
  SendGenericRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
          /*data*/) { return false; });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, "other");
  const auto* target_value =
      absl::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
}

// Test that generic method names is not scrubbed on the client side if
// the method attribute filter is set and it returns true.
TEST_F(OpenTelemetryPluginEnd2EndTest,
       GenericClientRpcWithMethodAttributeFilterReturningTrue) {
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kClientAttemptStartedInstrumentName})
          .set_generic_method_attribute_filter(
              [](absl::string_view /*generic_method*/) { return true; })));
  SendGenericRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
          /*data*/) { return false; });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kGenericMethodName);
  const auto* target_value =
      absl::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
}

// Test that generic method names are scrubbed properly on the server side.
TEST_F(OpenTelemetryPluginEnd2EndTest, GenericServerRpc) {
  Init(std::move(Options().set_metric_names(
      {grpc::OpenTelemetryPluginBuilder::kServerCallDurationInstrumentName})));
  SendGenericRPC();
  const char* kMetricName = "grpc.server.call.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, "other");
  const auto* status_value =
      absl::get_if<std::string>(&attributes.at("grpc.status"));
  ASSERT_NE(status_value, nullptr);
  EXPECT_EQ(*status_value, "UNIMPLEMENTED");
}

// Test that generic method names are scrubbed properly on the server side if
// the method attribute filter is set and it returns false.
TEST_F(OpenTelemetryPluginEnd2EndTest,
       GenericServerRpcWithMethodAttributeFilterReturningFalse) {
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kServerCallDurationInstrumentName})
          .set_generic_method_attribute_filter(
              [](absl::string_view /*generic_method*/) { return false; })));
  SendGenericRPC();
  const char* kMetricName = "grpc.server.call.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, "other");
  const auto* status_value =
      absl::get_if<std::string>(&attributes.at("grpc.status"));
  ASSERT_NE(status_value, nullptr);
  EXPECT_EQ(*status_value, "UNIMPLEMENTED");
}

// Test that generic method names are not scrubbed on the server side if
// the method attribute filter is set and it returns true.
TEST_F(OpenTelemetryPluginEnd2EndTest,
       GenericServerRpcWithMethodAttributeFilterReturningTrue) {
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kServerCallDurationInstrumentName})
          .set_generic_method_attribute_filter(
              [](absl::string_view /*generic_method*/) { return true; })));
  SendGenericRPC();
  const char* kMetricName = "grpc.server.call.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      absl::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kGenericMethodName);
  const auto* status_value =
      absl::get_if<std::string>(&attributes.at("grpc.status"));
  ASSERT_NE(status_value, nullptr);
  EXPECT_EQ(*status_value, "UNIMPLEMENTED");
}

using OpenTelemetryPluginOptionEnd2EndTest = OpenTelemetryPluginEnd2EndTest;

class SimpleLabelIterable : public grpc::internal::LabelsIterable {
 public:
  explicit SimpleLabelIterable(
      std::pair<absl::string_view, absl::string_view> label)
      : label_(label) {}

  absl::optional<std::pair<absl::string_view, absl::string_view>> Next()
      override {
    if (iterated_) {
      return absl::nullopt;
    }
    iterated_ = true;
    return label_;
  }

  size_t Size() const override { return 1; }

  void ResetIteratorPosition() override { iterated_ = false; }

 private:
  bool iterated_ = false;
  std::pair<absl::string_view, absl::string_view> label_;
};

class CustomLabelInjector : public grpc::internal::LabelsInjector {
 public:
  explicit CustomLabelInjector(std::pair<std::string, std::string> label)
      : label_(std::move(label)) {}
  ~CustomLabelInjector() override {}

  std::unique_ptr<grpc::internal::LabelsIterable> GetLabels(
      grpc_metadata_batch* /*incoming_initial_metadata*/) const override {
    return std::make_unique<SimpleLabelIterable>(label_);
  }

  void AddLabels(
      grpc_metadata_batch* /*outgoing_initial_metadata*/,
      grpc::internal::LabelsIterable* /*labels_from_incoming_metadata*/)
      const override {}

  bool AddOptionalLabels(
      bool /*is_client*/,
      absl::Span<const std::shared_ptr<std::map<std::string, std::string>>>
      /*optional_labels_span*/,
      opentelemetry::nostd::function_ref<
          bool(opentelemetry::nostd::string_view,
               opentelemetry::common::AttributeValue)>
      /*callback*/) const override {
    return true;
  }

  size_t GetOptionalLabelsSize(
      bool /*is_client*/,
      absl::Span<const std::shared_ptr<std::map<std::string, std::string>>>
      /*optional_labels_span*/) const override {
    return 0;
  }

 private:
  std::pair<std::string, std::string> label_;
};

class CustomPluginOption
    : public grpc::internal::InternalOpenTelemetryPluginOption {
 public:
  CustomPluginOption(bool enabled_on_client, bool enabled_on_server,
                     std::pair<std::string, std::string> label)
      : enabled_on_client_(enabled_on_client),
        enabled_on_server_(enabled_on_server),
        label_injector_(
            std::make_unique<CustomLabelInjector>(std::move(label))) {}

  ~CustomPluginOption() override {}

  bool IsActiveOnClientChannel(absl::string_view /*target*/) const override {
    return enabled_on_client_;
  }

  bool IsActiveOnServer(const grpc_core::ChannelArgs& /*args*/) const override {
    return enabled_on_server_;
  }

  const grpc::internal::LabelsInjector* labels_injector() const override {
    return label_injector_.get();
  }

 private:
  bool enabled_on_client_;
  bool enabled_on_server_;
  std::unique_ptr<CustomLabelInjector> label_injector_;
};

TEST_F(OpenTelemetryPluginOptionEnd2EndTest, Basic) {
  Init(
      std::move(Options()
                    .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptDurationInstrumentName,
                                       grpc::OpenTelemetryPluginBuilder::
                                           kServerCallDurationInstrumentName})
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ true, /*enabled_on_server*/ true,
                        std::make_pair("key", "value")))));
  SendRPC();
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) {
        return !data.contains("grpc.client.attempt.duration") ||
               !data.contains("grpc.server.call.duration");
      });
  // Verify client side metric
  ASSERT_EQ(data["grpc.client.attempt.duration"].size(), 1);
  const auto& client_attributes =
      data["grpc.client.attempt.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(client_attributes.size(), 4);
  EXPECT_EQ(absl::get<std::string>(client_attributes.at("key")), "value");
  // Verify server side metric
  ASSERT_EQ(data["grpc.server.call.duration"].size(), 1);
  const auto& server_attributes =
      data["grpc.server.call.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(server_attributes.size(), 3);
  EXPECT_EQ(absl::get<std::string>(server_attributes.at("key")), "value");
}

TEST_F(OpenTelemetryPluginOptionEnd2EndTest, ClientOnlyPluginOption) {
  Init(
      std::move(Options()
                    .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptDurationInstrumentName,
                                       grpc::OpenTelemetryPluginBuilder::
                                           kServerCallDurationInstrumentName})
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ true, /*enabled_on_server*/ false,
                        std::make_pair("key", "value")))));
  SendRPC();
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) {
        return !data.contains("grpc.client.attempt.duration") ||
               !data.contains("grpc.server.call.duration");
      });
  // Verify client side metric
  ASSERT_EQ(data["grpc.client.attempt.duration"].size(), 1);
  const auto& client_attributes =
      data["grpc.client.attempt.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(client_attributes.size(), 4);
  EXPECT_EQ(absl::get<std::string>(client_attributes.at("key")), "value");
  // Verify server side metric
  ASSERT_EQ(data["grpc.server.call.duration"].size(), 1);
  const auto& server_attributes =
      data["grpc.server.call.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(server_attributes.size(), 2);
  EXPECT_THAT(server_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("key"))));
}

TEST_F(OpenTelemetryPluginOptionEnd2EndTest, ServerOnlyPluginOption) {
  Init(
      std::move(Options()
                    .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptDurationInstrumentName,
                                       grpc::OpenTelemetryPluginBuilder::
                                           kServerCallDurationInstrumentName})
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ false, /*enabled_on_server*/ true,
                        std::make_pair("key", "value")))));
  SendRPC();
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) {
        return !data.contains("grpc.client.attempt.duration") ||
               !data.contains("grpc.server.call.duration");
      });
  // Verify client side metric
  ASSERT_EQ(data["grpc.client.attempt.duration"].size(), 1);
  const auto& attributes =
      data["grpc.client.attempt.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 3);
  EXPECT_THAT(attributes,
              ::testing::Not(::testing::Contains(::testing::Key("key"))));
  // Verify server side metric
  ASSERT_EQ(data["grpc.server.call.duration"].size(), 1);
  const auto& server_attributes =
      data["grpc.server.call.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(server_attributes.size(), 3);
  EXPECT_EQ(absl::get<std::string>(server_attributes.at("key")), "value");
}

TEST_F(OpenTelemetryPluginOptionEnd2EndTest,
       MultipleEnabledAndDisabledPluginOptions) {
  Init(
      std::move(Options()
                    .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptDurationInstrumentName,
                                       grpc::OpenTelemetryPluginBuilder::
                                           kServerCallDurationInstrumentName})
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ true, /*enabled_on_server*/ true,
                        std::make_pair("key1", "value1")))
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ true, /*enabled_on_server*/ false,
                        std::make_pair("key2", "value2")))
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ true, /*enabled_on_server*/ false,
                        std::make_pair("key3", "value3")))
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ false, /*enabled_on_server*/ true,
                        std::make_pair("key4", "value4")))
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ false, /*enabled_on_server*/ true,
                        std::make_pair("key5", "value5")))));
  SendRPC();
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) {
        return !data.contains("grpc.client.attempt.duration") ||
               !data.contains("grpc.server.call.duration");
      });
  // Verify client side metric
  ASSERT_EQ(data["grpc.client.attempt.duration"].size(), 1);
  const auto& client_attributes =
      data["grpc.client.attempt.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(client_attributes.size(), 6);
  EXPECT_EQ(absl::get<std::string>(client_attributes.at("key1")), "value1");
  EXPECT_EQ(absl::get<std::string>(client_attributes.at("key2")), "value2");
  EXPECT_EQ(absl::get<std::string>(client_attributes.at("key3")), "value3");
  EXPECT_THAT(client_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("key4"))));
  EXPECT_THAT(client_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("key5"))));
  // Verify server side metric
  ASSERT_EQ(data["grpc.server.call.duration"].size(), 1);
  const auto& server_attributes =
      data["grpc.server.call.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(server_attributes.size(), 5);
  EXPECT_EQ(absl::get<std::string>(server_attributes.at("key1")), "value1");
  EXPECT_THAT(server_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("key2"))));
  EXPECT_THAT(server_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("key3"))));
  EXPECT_EQ(absl::get<std::string>(server_attributes.at("key4")), "value4");
  EXPECT_EQ(absl::get<std::string>(server_attributes.at("key5")), "value5");
}

using OpenTelemetryPluginNPCMetricsTest = OpenTelemetryPluginEnd2EndTest;

template <typename HandleType, typename ValueType>
void VerifyCounter(
    absl::AnyInvocable<HandleType()> register_function,
    absl::AnyInvocable<void()> init_function,
    absl::AnyInvocable<absl::flat_hash_map<
        std::string,
        std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>()>
        read_metrics_data_function,
    std::vector<ValueType> counter_values, ValueType result,
    absl::string_view metric_name,
    absl::Span<const absl::string_view> label_keys,
    absl::Span<const absl::string_view> label_values,
    absl::Span<const absl::string_view> optional_label_keys,
    absl::Span<const absl::string_view> optional_label_values) {
  HandleType handle = register_function();
  init_function();
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::StatsPlugin::ChannelScope("dns:///localhost:8080", ""));
  ASSERT_EQ(stats_plugins.size(), 1);
  for (auto v : counter_values) {
    stats_plugins.AddCounter(handle, v, label_values, optional_label_values);
  }
  auto data = read_metrics_data_function();
  ASSERT_EQ(data[metric_name].size(), 1);
  EXPECT_TRUE(opentelemetry::nostd::holds_alternative<
              opentelemetry::sdk::metrics::SumPointData>(
      data[metric_name][0].point_data));
  EXPECT_EQ(
      opentelemetry::nostd::get<ValueType>(
          opentelemetry::nostd::get<opentelemetry::sdk::metrics::SumPointData>(
              data[metric_name][0].point_data)
              .value_),
      result);
  const auto& attributes = data[metric_name][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), label_keys.size() + optional_label_keys.size());
  for (int i = 0; i < label_keys.size(); i++) {
    EXPECT_EQ(absl::get<std::string>(attributes.at(std::string(label_keys[i]))),
              label_values[i]);
  }
  for (int i = 0; i < optional_label_keys.size(); i++) {
    EXPECT_EQ(absl::get<std::string>(
                  attributes.at(std::string(optional_label_keys[i]))),
              optional_label_values[i]);
  }
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordUInt64Counter) {
  constexpr absl::string_view kMetricName = "uint64_counter";
  constexpr absl::string_view kLabelKeys[] = {"label_key_1", "label_key_2"};
  constexpr absl::string_view kOptionalLabelKeys[] = {"optional_label_key_1",
                                                      "optional_label_key_2"};
  constexpr absl::string_view kLabelValues[] = {"label_value_1",
                                                "label_value_2"};
  constexpr absl::string_view kOptionalLabelValues[] = {
      "optional_label_value_1", "optional_label_value_2"};
  VerifyCounter<grpc_core::GlobalInstrumentsRegistry::GlobalUInt64CounterHandle,
                int64_t>(
      [&]() {
        return grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
            kMetricName, "A simple uint64 counter.", "unit", kLabelKeys,
            kOptionalLabelKeys, /*enable_by_default=*/true);
      },
      [&, this]() {
        Init(std::move(Options()
                           .set_metric_names({kMetricName})
                           .set_target_selector([](absl::string_view target) {
                             return absl::StartsWith(target, "dns:///");
                           })
                           .add_optional_label(kOptionalLabelKeys[0])
                           .add_optional_label(kOptionalLabelKeys[1])));
      },
      [&, this]() {
        return ReadCurrentMetricsData(
            [&](const absl::flat_hash_map<
                std::string,
                std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                    data) { return !data.contains(kMetricName); });
      },
      {1, 2, 3}, 6, kMetricName, kLabelKeys, kLabelValues, kOptionalLabelKeys,
      kOptionalLabelValues);
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordDoubleCounter) {
  constexpr absl::string_view kMetricName = "double_counter";
  constexpr absl::string_view kLabelKeys[] = {"label_key_1", "label_key_2"};
  constexpr absl::string_view kOptionalLabelKeys[] = {"optional_label_key_1",
                                                      "optional_label_key_2"};
  constexpr absl::string_view kLabelValues[] = {"label_value_1",
                                                "label_value_2"};
  constexpr absl::string_view kOptionalLabelValues[] = {
      "optional_label_value_1", "optional_label_value_2"};
  VerifyCounter<grpc_core::GlobalInstrumentsRegistry::GlobalDoubleCounterHandle,
                double>(
      [&]() {
        return grpc_core::GlobalInstrumentsRegistry::RegisterDoubleCounter(
            kMetricName, "A simple double counter.", "unit", kLabelKeys,
            kOptionalLabelKeys, /*enable_by_default=*/false);
      },
      [&, this]() {
        Init(std::move(Options()
                           .set_metric_names({kMetricName})
                           .set_target_selector([](absl::string_view target) {
                             return absl::StartsWith(target, "dns:///");
                           })
                           .add_optional_label(kOptionalLabelKeys[0])
                           .add_optional_label(kOptionalLabelKeys[1])));
      },
      [&, this]() {
        return ReadCurrentMetricsData(
            [&](const absl::flat_hash_map<
                std::string,
                std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                    data) { return !data.contains(kMetricName); });
      },
      {1.23, 2.34, 3.45}, 7.02, kMetricName, kLabelKeys, kLabelValues,
      kOptionalLabelKeys, kOptionalLabelValues);
}

template <typename ValueType>
void ExpectEqual(ValueType v, ValueType expectation) {
  EXPECT_EQ(v, expectation);
}

template <>
void ExpectEqual(double v, double expectation) {
  EXPECT_THAT(v, ::testing::DoubleEq(expectation));
}

template <typename HandleType, typename ValueType>
void VerifyHistogram(
    absl::AnyInvocable<HandleType()> register_function,
    absl::AnyInvocable<void()> init_function,
    absl::AnyInvocable<absl::flat_hash_map<
        std::string,
        std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>()>
        read_metrics_data_function,
    std::vector<ValueType> histogram_values, ValueType sum, ValueType min,
    ValueType max, uint64_t count, absl::string_view metric_name,
    absl::Span<const absl::string_view> label_keys,
    absl::Span<const absl::string_view> label_values,
    absl::Span<const absl::string_view> optional_label_keys,
    absl::Span<const absl::string_view> optional_label_values,
    size_t num_plugins = 1) {
  HandleType handle = register_function();
  init_function();
  grpc_core::ChannelArgs args;
  args = args.Set(GRPC_ARG_SERVER_SELECTOR_KEY, GRPC_ARG_SERVER_SELECTOR_VALUE);
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
  ASSERT_EQ(stats_plugins.size(), num_plugins);
  for (auto v : histogram_values) {
    stats_plugins.RecordHistogram(handle, v, label_values,
                                  optional_label_values);
  }
  auto data = read_metrics_data_function();
  ASSERT_EQ(data[metric_name].size(), 1);
  EXPECT_TRUE(opentelemetry::nostd::holds_alternative<
              opentelemetry::sdk::metrics::HistogramPointData>(
      data[metric_name][0].point_data));
  ExpectEqual(opentelemetry::nostd::get<ValueType>(
                  opentelemetry::nostd::get<
                      opentelemetry::sdk::metrics::HistogramPointData>(
                      data[metric_name][0].point_data)
                      .sum_),
              sum);
  EXPECT_EQ(opentelemetry::nostd::get<ValueType>(
                opentelemetry::nostd::get<
                    opentelemetry::sdk::metrics::HistogramPointData>(
                    data[metric_name][0].point_data)
                    .min_),
            min);
  EXPECT_EQ(opentelemetry::nostd::get<ValueType>(
                opentelemetry::nostd::get<
                    opentelemetry::sdk::metrics::HistogramPointData>(
                    data[metric_name][0].point_data)
                    .max_),
            max);
  EXPECT_EQ(opentelemetry::nostd::get<
                opentelemetry::sdk::metrics::HistogramPointData>(
                data[metric_name][0].point_data)
                .count_,
            count);
  const auto& attributes = data[metric_name][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), label_keys.size() + optional_label_keys.size());
  for (int i = 0; i < label_keys.size(); i++) {
    EXPECT_EQ(absl::get<std::string>(attributes.at(std::string(label_keys[i]))),
              label_values[i]);
  }
  for (int i = 0; i < optional_label_keys.size(); i++) {
    EXPECT_EQ(absl::get<std::string>(
                  attributes.at(std::string(optional_label_keys[i]))),
              optional_label_values[i]);
  }
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordUInt64Histogram) {
  constexpr absl::string_view kMetricName = "uint64_histogram";
  constexpr absl::string_view kLabelKeys[] = {"label_key_1", "label_key_2"};
  constexpr absl::string_view kOptionalLabelKeys[] = {"optional_label_key_1",
                                                      "optional_label_key_2"};
  constexpr absl::string_view kLabelValues[] = {"label_value_1",
                                                "label_value_2"};
  constexpr absl::string_view kOptionalLabelValues[] = {
      "optional_label_value_1", "optional_label_value_2"};
  VerifyHistogram<
      grpc_core::GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle,
      int64_t>(
      [&]() {
        return grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Histogram(
            kMetricName, "A simple uint64 histogram.", "unit", kLabelKeys,
            kOptionalLabelKeys, /*enable_by_default=*/true);
      },
      [&, this]() {
        Init(std::move(
            Options()
                .set_metric_names({kMetricName})
                .set_server_selector([](const grpc_core::ChannelArgs& args) {
                  return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                         GRPC_ARG_SERVER_SELECTOR_VALUE;
                })
                .add_optional_label(kOptionalLabelKeys[0])
                .add_optional_label(kOptionalLabelKeys[1])));
      },
      [&, this]() {
        return ReadCurrentMetricsData(
            [&](const absl::flat_hash_map<
                std::string,
                std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                    data) { return !data.contains(kMetricName); });
      },
      {1, 1, 2, 3, 4, 4, 5, 6}, 26, 1, 6, 8, kMetricName, kLabelKeys,
      kLabelValues, kOptionalLabelKeys, kOptionalLabelValues);
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordDoubleHistogram) {
  constexpr absl::string_view kMetricName = "double_histogram";
  constexpr absl::string_view kLabelKeys[] = {"label_key_1", "label_key_2"};
  constexpr absl::string_view kOptionalLabelKeys[] = {"optional_label_key_1",
                                                      "optional_label_key_2"};
  constexpr absl::string_view kLabelValues[] = {"label_value_1",
                                                "label_value_2"};
  constexpr absl::string_view kOptionalLabelValues[] = {
      "optional_label_value_1", "optional_label_value_2"};
  VerifyHistogram<
      grpc_core::GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle,
      double>(
      [&]() {
        return grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
            kMetricName, "A simple double histogram.", "unit", kLabelKeys,
            kOptionalLabelKeys, /*enable_by_default=*/true);
      },
      [&, this]() {
        Init(std::move(
            Options()
                .set_metric_names({kMetricName})
                .set_server_selector([](const grpc_core::ChannelArgs& args) {
                  return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                         GRPC_ARG_SERVER_SELECTOR_VALUE;
                })
                .add_optional_label(kOptionalLabelKeys[0])
                .add_optional_label(kOptionalLabelKeys[1])));
      },
      [&, this]() {
        return ReadCurrentMetricsData(
            [&](const absl::flat_hash_map<
                std::string,
                std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                    data) { return !data.contains(kMetricName); });
      },
      {1.1, 1.2, 2.2, 3.3, 4.4, 4.5, 5.5, 6.6}, 28.8, 1.1, 6.6, 8, kMetricName,
      kLabelKeys, kLabelValues, kOptionalLabelKeys, kOptionalLabelValues);
}

TEST_F(OpenTelemetryPluginNPCMetricsTest,
       RegisterMultipleOpenTelemetryPlugins) {
  constexpr absl::string_view kMetricName = "double_histogram";
  constexpr absl::string_view kLabelKeys[] = {"label_key_1", "label_key_2"};
  constexpr absl::string_view kOptionalLabelKeys[] = {"optional_label_key_1",
                                                      "optional_label_key_2"};
  constexpr absl::string_view kLabelValues[] = {"label_value_1",
                                                "label_value_2"};
  constexpr absl::string_view kOptionalLabelValues[] = {
      "optional_label_value_1", "optional_label_value_2"};
  auto instrument_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          kMetricName, "A simple double histogram.", "unit", kLabelKeys,
          kOptionalLabelKeys, /*enable_by_default=*/true);
  // Build and register a separate OpenTelemetryPlugin and verify its histogram
  // recording.
  grpc::internal::OpenTelemetryPluginBuilderImpl ot_builder;
  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader;
  ConfigureOpenTelemetryPluginBuilderWithOptions(
      std::move(
          Options()
              .set_metric_names({kMetricName})
              .set_server_selector([](const grpc_core::ChannelArgs& args) {
                return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                       GRPC_ARG_SERVER_SELECTOR_VALUE;
              })
              .add_optional_label(kOptionalLabelKeys[0])
              .add_optional_label(kOptionalLabelKeys[1])),
      &ot_builder, &reader);
  EXPECT_EQ(ot_builder.BuildAndRegisterGlobal(), absl::OkStatus());
  VerifyHistogram<
      grpc_core::GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle,
      double>(
      [&]() { return instrument_handle; }, []() {},
      [&]() {
        return grpc::testing::ReadCurrentMetricsData(
            reader,
            [&](const absl::flat_hash_map<
                std::string,
                std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                    data) { return !data.contains(kMetricName); });
      },
      {1.23, 2.34, 3.45, 4.56}, 11.58, 1.23, 4.56, 4, kMetricName, kLabelKeys,
      kLabelValues, kOptionalLabelKeys, kOptionalLabelValues);
  // Now build and register another OpenTelemetryPlugin using the test fixture.
  // Note this will record to the first plugin as well.
  VerifyHistogram<
      grpc_core::GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle,
      double>(
      [&]() { return instrument_handle; },
      [&, this]() {
        Init(std::move(
            Options()
                .set_metric_names({kMetricName})
                .set_server_selector([](const grpc_core::ChannelArgs& args) {
                  return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                         GRPC_ARG_SERVER_SELECTOR_VALUE;
                })
                .add_optional_label(kOptionalLabelKeys[0])
                .add_optional_label(kOptionalLabelKeys[1])));
      },
      [&, this]() {
        return ReadCurrentMetricsData(
            [&](const absl::flat_hash_map<
                std::string,
                std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                    data) { return !data.contains(kMetricName); });
      },
      {1.1, 1.2, 2.2, 3.3, 4.4, 4.5, 5.5, 6.6}, 28.8, 1.1, 6.6, 8, kMetricName,
      kLabelKeys, kLabelValues, kOptionalLabelKeys, kOptionalLabelValues, 2);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
