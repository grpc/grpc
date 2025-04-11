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

#include <grpcpp/ext/otel_plugin.h>
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <ratio>
#include <type_traits>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/metrics/data/metric_data.h"
#include "opentelemetry/sdk/metrics/data/point_data.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/telemetry/call_tracer.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/ext/otel/otel_test_library.h"

namespace grpc {
namespace testing {
namespace {

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
  auto point_data = std::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = std::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      std::get_if<std::string>(&attributes.at("grpc.target"));
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
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 3);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      std::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
  const auto* status_value =
      std::get_if<std::string>(&attributes.at("grpc.status"));
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
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  ASSERT_EQ(std::get<int64_t>(point_data->max_), 5);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 3);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      std::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
  const auto* status_value =
      std::get_if<std::string>(&attributes.at("grpc.status"));
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
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  ASSERT_EQ(std::get<int64_t>(point_data->max_), 5);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 3);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      std::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
  const auto* status_value =
      std::get_if<std::string>(&attributes.at("grpc.status"));
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
  auto point_data = std::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto server_started_value = std::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(server_started_value, nullptr);
  ASSERT_EQ(*server_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 1);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
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
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* status_value =
      std::get_if<std::string>(&attributes.at("grpc.status"));
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
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  EXPECT_EQ(point_data->count_, 1);
  ASSERT_EQ(std::get<int64_t>(point_data->max_), 5);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* status_value =
      std::get_if<std::string>(&attributes.at("grpc.status"));
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
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  ASSERT_EQ(std::get<int64_t>(point_data->max_), 5);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* status_value =
      std::get_if<std::string>(&attributes.at("grpc.status"));
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

// Test that the otel plugin sees the expected channel target and default
// authority.
TEST_F(OpenTelemetryPluginEnd2EndTest, VerifyChannelScopeTargetAndAuthority) {
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kClientAttemptStartedInstrumentName})
          .set_channel_scope_filter(
              [&](const OpenTelemetryPluginBuilder::ChannelScope& scope) {
                return scope.target() == canonical_server_address_ &&
                       scope.default_authority() == server_address_;
              })));
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
}

// Test that a channel scope filter returning true records metrics on the
// channel.
TEST_F(OpenTelemetryPluginEnd2EndTest, ChannelScopeFilterReturnsTrue) {
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kClientAttemptStartedInstrumentName})
          .set_channel_scope_filter(
              [](const OpenTelemetryPluginBuilder::ChannelScope& /*scope*/) {
                return true;
              })));
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = std::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = std::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      std::get_if<std::string>(&attributes.at("grpc.target"));
  ASSERT_NE(target_value, nullptr);
  EXPECT_EQ(*target_value, canonical_server_address_);
}

// Test that a channel scope filter returning false does not record metrics on
// the channel.
TEST_F(OpenTelemetryPluginEnd2EndTest, ChannelScopeFilterReturnsFalse) {
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kClientAttemptStartedInstrumentName})
          .set_channel_scope_filter(
              [](const OpenTelemetryPluginBuilder::ChannelScope& /*scope*/) {
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
  EXPECT_EQ(std::get<std::string>(server_attributes.at("grpc.method")),
            kMethodName);
  EXPECT_EQ(std::get<std::string>(server_attributes.at("grpc.status")), "OK");
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
  auto point_data = std::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = std::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      std::get_if<std::string>(&attributes.at("grpc.target"));
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
  auto point_data = std::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = std::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kMethodName);
  const auto* target_value =
      std::get_if<std::string>(&attributes.at("grpc.target"));
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
  auto point_data = std::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = std::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, "other");
  const auto* target_value =
      std::get_if<std::string>(&attributes.at("grpc.target"));
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
  auto point_data = std::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = std::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, "other");
  const auto* target_value =
      std::get_if<std::string>(&attributes.at("grpc.target"));
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
  auto point_data = std::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = std::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kGenericMethodName);
  const auto* target_value =
      std::get_if<std::string>(&attributes.at("grpc.target"));
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
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, "other");
  const auto* status_value =
      std::get_if<std::string>(&attributes.at("grpc.status"));
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
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, "other");
  const auto* status_value =
      std::get_if<std::string>(&attributes.at("grpc.status"));
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
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(attributes.size(), 2);
  const auto* method_value =
      std::get_if<std::string>(&attributes.at("grpc.method"));
  ASSERT_NE(method_value, nullptr);
  EXPECT_EQ(*method_value, kGenericMethodName);
  const auto* status_value =
      std::get_if<std::string>(&attributes.at("grpc.status"));
  ASSERT_NE(status_value, nullptr);
  EXPECT_EQ(*status_value, "UNIMPLEMENTED");
}

TEST_F(OpenTelemetryPluginEnd2EndTest, OptionalPerCallLocalityLabel) {
  Init(
      std::move(Options()
                    .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptStartedInstrumentName,
                                       grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptDurationInstrumentName,
                                       grpc::OpenTelemetryPluginBuilder::
                                           kServerCallStartedInstrumentName,
                                       grpc::OpenTelemetryPluginBuilder::
                                           kServerCallDurationInstrumentName})
                    .add_optional_label("grpc.lb.locality")
                    .set_labels_to_inject(
                        {{grpc_core::ClientCallTracer::CallAttemptTracer::
                              OptionalLabelKey::kLocality,
                          grpc_core::RefCountedStringValue("locality")}})));
  SendRPC();
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) {
        return !data.contains("grpc.client.attempt.started") ||
               !data.contains("grpc.client.attempt.duration") ||
               !data.contains("grpc.server.call.started") ||
               !data.contains("grpc.server.call.duration");
      });
  // Verify client side metric (grpc.client.attempt.started) does not sees this
  // label
  ASSERT_EQ(data["grpc.client.attempt.started"].size(), 1);
  const auto& client_attributes =
      data["grpc.client.attempt.started"][0].attributes.GetAttributes();
  EXPECT_THAT(
      client_attributes,
      ::testing::Not(::testing::Contains(::testing::Key("grpc.lb.locality"))));
  // Verify client side metric (grpc.client.attempt.duration) sees this label.
  ASSERT_EQ(data["grpc.client.attempt.duration"].size(), 1);
  const auto& client_duration_attributes =
      data["grpc.client.attempt.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(
      std::get<std::string>(client_duration_attributes.at("grpc.lb.locality")),
      "locality");
  // Verify server metric (grpc.server.call.started) does not see this label
  ASSERT_EQ(data["grpc.server.call.started"].size(), 1);
  const auto& server_attributes =
      data["grpc.server.call.started"][0].attributes.GetAttributes();
  EXPECT_THAT(
      server_attributes,
      ::testing::Not(::testing::Contains(::testing::Key("grpc.lb.locality"))));
  // Verify server metric (grpc.server.call.duration) does not see this label
  ASSERT_EQ(data["grpc.server.call.duration"].size(), 1);
  const auto& server_duration_attributes =
      data["grpc.server.call.duration"][0].attributes.GetAttributes();
  EXPECT_THAT(
      server_duration_attributes,
      ::testing::Not(::testing::Contains(::testing::Key("grpc.lb.locality"))));
}

// Tests that when locality label is enabled on the plugin but not provided by
// gRPC, an empty value is recorded.
TEST_F(OpenTelemetryPluginEnd2EndTest,
       OptionalPerCallLocalityLabelWhenNotAvailable) {
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kClientAttemptDurationInstrumentName})
          .add_optional_label("grpc.lb.locality")));
  SendRPC();
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains("grpc.client.attempt.duration"); });
  // Verify client side metric (grpc.client.attempt.duration) sees the empty
  // label value.
  ASSERT_EQ(data["grpc.client.attempt.duration"].size(), 1);
  const auto& client_duration_attributes =
      data["grpc.client.attempt.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(
      std::get<std::string>(client_duration_attributes.at("grpc.lb.locality")),
      "");
}

// Tests that when locality label is injected but not enabled by the plugin, the
// label is not recorded.
TEST_F(OpenTelemetryPluginEnd2EndTest,
       OptionalPerCallLocalityLabelNotRecordedWhenNotEnabled) {
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kClientAttemptDurationInstrumentName})
          .set_labels_to_inject(
              {{grpc_core::ClientCallTracer::CallAttemptTracer::
                    OptionalLabelKey::kLocality,
                grpc_core::RefCountedStringValue("locality")}})));
  SendRPC();
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains("grpc.client.attempt.duration"); });
  // Verify client side metric (grpc.client.attempt.duration) does not see the
  // locality label.
  ASSERT_EQ(data["grpc.client.attempt.duration"].size(), 1);
  const auto& client_duration_attributes =
      data["grpc.client.attempt.duration"][0].attributes.GetAttributes();
  EXPECT_THAT(
      client_duration_attributes,
      ::testing::Not(::testing::Contains(::testing::Key("grpc.lb.locality"))));
}

TEST_F(OpenTelemetryPluginEnd2EndTest,
       UnknownLabelDoesNotShowOnPerCallMetrics) {
  Init(
      std::move(Options()
                    .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptStartedInstrumentName,
                                       grpc::OpenTelemetryPluginBuilder::
                                           kClientAttemptDurationInstrumentName,
                                       grpc::OpenTelemetryPluginBuilder::
                                           kServerCallStartedInstrumentName,
                                       grpc::OpenTelemetryPluginBuilder::
                                           kServerCallDurationInstrumentName})
                    .add_optional_label("unknown")));
  SendRPC();
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) {
        return !data.contains("grpc.client.attempt.started") ||
               !data.contains("grpc.client.attempt.duration") ||
               !data.contains("grpc.server.call.started") ||
               !data.contains("grpc.server.call.duration");
      });
  // Verify client side metric (grpc.client.attempt.started) does not sees this
  // label
  ASSERT_EQ(data["grpc.client.attempt.started"].size(), 1);
  const auto& client_attributes =
      data["grpc.client.attempt.started"][0].attributes.GetAttributes();
  EXPECT_THAT(client_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("unknown"))));
  // Verify client side metric (grpc.client.attempt.duration) does not see this
  // label
  ASSERT_EQ(data["grpc.client.attempt.duration"].size(), 1);
  const auto& client_duration_attributes =
      data["grpc.client.attempt.duration"][0].attributes.GetAttributes();
  EXPECT_THAT(client_duration_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("unknown"))));
  // Verify server metric (grpc.server.call.started) does not see this label
  ASSERT_EQ(data["grpc.server.call.started"].size(), 1);
  const auto& server_attributes =
      data["grpc.server.call.started"][0].attributes.GetAttributes();
  EXPECT_THAT(
      server_attributes,
      ::testing::Not(::testing::Contains(::testing::Key("grpc.lb.locality"))));
  // Verify server metric (grpc.server.call.duration) does not see this label
  ASSERT_EQ(data["grpc.server.call.duration"].size(), 1);
  const auto& server_duration_attributes =
      data["grpc.server.call.duration"][0].attributes.GetAttributes();
  EXPECT_THAT(server_duration_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("unknown"))));
}

TEST(OpenTelemetryPluginMetricsEnablingDisablingTest, TestEnableDisableAPIs) {
  grpc::internal::OpenTelemetryPluginBuilderImpl builder;
  // First disable all metrics
  builder.DisableAllMetrics();
  EXPECT_TRUE(builder.TestOnlyEnabledMetrics().empty());
  // Add in a few metrics
  builder.EnableMetrics(
      {"grpc.test.metric_1", "grpc.test.metric_2", "grpc.test.metric_3"});
  EXPECT_THAT(
      builder.TestOnlyEnabledMetrics(),
      ::testing::UnorderedElementsAre(
          "grpc.test.metric_1", "grpc.test.metric_2", "grpc.test.metric_3"));
  // Now remove a few metrics
  builder.DisableMetrics({"grpc.test.metric_1", "grpc.test.metric_2"});
  EXPECT_THAT(builder.TestOnlyEnabledMetrics(),
              ::testing::UnorderedElementsAre("grpc.test.metric_3"));
}

TEST_F(OpenTelemetryPluginEnd2EndTest, RegisterMultipleStatsPluginsPerChannel) {
  std::shared_ptr<grpc::experimental::OpenTelemetryPlugin> plugin1;
  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader1;
  std::tie(plugin1, reader1) = BuildOpenTelemetryPlugin(std::move(
      Options().set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                      kClientAttemptDurationInstrumentName})));
  std::shared_ptr<grpc::experimental::OpenTelemetryPlugin> plugin2;
  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader2;
  std::tie(plugin2, reader2) = BuildOpenTelemetryPlugin(std::move(
      Options().set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                      kClientAttemptDurationInstrumentName})));
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kClientAttemptDurationInstrumentName})
          .add_per_channel_stats_plugin(std::move(plugin1))
          .add_per_channel_stats_plugin(std::move(plugin2))));
  const std::vector<absl::string_view> kLabelKeys = {
      "grpc.method", "grpc.target", "grpc.status"};
  const std::vector<absl::string_view> kLabelValues = {
      kMethodName, canonical_server_address_, "OK"};
  const std::vector<absl::string_view> kOptionalLabelKeys = {};
  const std::vector<absl::string_view> kOptionalLabelValues = {};
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(
      data,
      ::testing::ElementsAre(::testing::Pair(
          kMetricName,
          ::testing::ElementsAre(::testing::AllOf(
              AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                           kOptionalLabelValues),
              ::testing::Field(
                  &opentelemetry::sdk::metrics::PointDataAttributes::point_data,
                  ::testing::VariantWith<
                      opentelemetry::sdk::metrics::HistogramPointData>(
                      ::testing::Field(&opentelemetry::sdk::metrics::
                                           HistogramPointData::count_,
                                       ::testing::Eq(1)))))))));
  data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); },
      reader1.get());
  EXPECT_THAT(
      data,
      ::testing::ElementsAre(::testing::Pair(
          kMetricName,
          ::testing::ElementsAre(::testing::AllOf(
              AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                           kOptionalLabelValues),
              ::testing::Field(
                  &opentelemetry::sdk::metrics::PointDataAttributes::point_data,
                  ::testing::VariantWith<
                      opentelemetry::sdk::metrics::HistogramPointData>(
                      ::testing::Field(&opentelemetry::sdk::metrics::
                                           HistogramPointData::count_,
                                       ::testing::Eq(1)))))))));
  data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); },
      reader2.get());
  EXPECT_THAT(
      data,
      ::testing::ElementsAre(::testing::Pair(
          kMetricName,
          ::testing::ElementsAre(::testing::AllOf(
              AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                           kOptionalLabelValues),
              ::testing::Field(
                  &opentelemetry::sdk::metrics::PointDataAttributes::point_data,
                  ::testing::VariantWith<
                      opentelemetry::sdk::metrics::HistogramPointData>(
                      ::testing::Field(&opentelemetry::sdk::metrics::
                                           HistogramPointData::count_,
                                       ::testing::Eq(1)))))))));
}

TEST_F(OpenTelemetryPluginEnd2EndTest,
       RegisterSameStatsPluginForMultipleChannels) {
  // channel1                         channel2
  //    |                                |
  //    | (global plugin, plugin1)       | (global plugin, plugin1, plugin2)
  //    |                                |
  std::shared_ptr<grpc::experimental::OpenTelemetryPlugin> plugin1;
  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader1;
  std::tie(plugin1, reader1) = BuildOpenTelemetryPlugin(std::move(
      Options().set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                      kClientAttemptDurationInstrumentName})));
  std::shared_ptr<grpc::experimental::OpenTelemetryPlugin> plugin2;
  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader2;
  std::tie(plugin2, reader2) = BuildOpenTelemetryPlugin(std::move(
      Options().set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                      kClientAttemptDurationInstrumentName})));
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kClientAttemptDurationInstrumentName})
          .add_per_channel_stats_plugin(plugin1)));
  // Adds the same plugin to another channel.
  ChannelArguments channel_args;
  plugin1->AddToChannelArguments(&channel_args);
  plugin2->AddToChannelArguments(&channel_args);
  auto channel2 = grpc::CreateCustomChannel(
      server_address_, grpc::InsecureChannelCredentials(), channel_args);
  std::unique_ptr<EchoTestService::Stub> stub =
      EchoTestService::NewStub(std::move(channel2));
  // Sends 2 RPCs, one from each channel.
  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  grpc::ClientContext context;
  auto status = stub->Echo(&context, request, &response);
  ASSERT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  SendRPC();
  const std::vector<absl::string_view> kLabelKeys = {
      "grpc.method", "grpc.target", "grpc.status"};
  const std::vector<absl::string_view> kLabelValues = {
      kMethodName, canonical_server_address_, "OK"};
  const std::vector<absl::string_view> kOptionalLabelKeys = {};
  const std::vector<absl::string_view> kOptionalLabelValues = {};
  const char* kMetricName = "grpc.client.attempt.duration";
  // Verifies that we got 2 histogram points in global plugin.
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(
      data,
      ::testing::ElementsAre(::testing::Pair(
          kMetricName,
          ::testing::ElementsAre(::testing::AllOf(
              AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                           kOptionalLabelValues),
              ::testing::Field(
                  &opentelemetry::sdk::metrics::PointDataAttributes::point_data,
                  ::testing::VariantWith<
                      opentelemetry::sdk::metrics::HistogramPointData>(
                      ::testing::Field(&opentelemetry::sdk::metrics::
                                           HistogramPointData::count_,
                                       ::testing::Eq(2)))))))));
  // Verifies that we got 2 histogram points in plugin1.
  data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); },
      reader1.get());
  EXPECT_THAT(
      data,
      ::testing::ElementsAre(::testing::Pair(
          kMetricName,
          ::testing::ElementsAre(::testing::AllOf(
              AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                           kOptionalLabelValues),
              ::testing::Field(
                  &opentelemetry::sdk::metrics::PointDataAttributes::point_data,
                  ::testing::VariantWith<
                      opentelemetry::sdk::metrics::HistogramPointData>(
                      ::testing::Field(&opentelemetry::sdk::metrics::
                                           HistogramPointData::count_,
                                       ::testing::Eq(2)))))))));
  // Verifies that we got 1 histogram point in plugin2.
  data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); },
      reader2.get());
  EXPECT_THAT(
      data,
      ::testing::ElementsAre(::testing::Pair(
          kMetricName,
          ::testing::ElementsAre(::testing::AllOf(
              AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                           kOptionalLabelValues),
              ::testing::Field(
                  &opentelemetry::sdk::metrics::PointDataAttributes::point_data,
                  ::testing::VariantWith<
                      opentelemetry::sdk::metrics::HistogramPointData>(
                      ::testing::Field(&opentelemetry::sdk::metrics::
                                           HistogramPointData::count_,
                                       ::testing::Eq(1)))))))));
}

TEST_F(OpenTelemetryPluginEnd2EndTest, RegisterMultipleStatsPluginsPerServer) {
  std::shared_ptr<grpc::experimental::OpenTelemetryPlugin> plugin;
  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader;
  std::tie(plugin, reader) = BuildOpenTelemetryPlugin(std::move(
      Options().set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                      kServerCallDurationInstrumentName})));
  Init(std::move(Options()
                     .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                            kServerCallDurationInstrumentName})
                     .add_per_server_stats_plugin(std::move(plugin))));
  const std::vector<absl::string_view> kLabelKeys = {"grpc.method",
                                                     "grpc.status"};
  const std::vector<absl::string_view> kLabelValues = {kMethodName, "OK"};
  const std::vector<absl::string_view> kOptionalLabelKeys = {};
  const std::vector<absl::string_view> kOptionalLabelValues = {};
  SendRPC();
  const char* kMetricName = "grpc.server.call.duration";
  // Verifies that both plugins have the server-side metrics recorded.
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(
      data,
      ::testing::ElementsAre(::testing::Pair(
          kMetricName,
          ::testing::ElementsAre(::testing::AllOf(
              AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                           kOptionalLabelValues),
              ::testing::Field(
                  &opentelemetry::sdk::metrics::PointDataAttributes::point_data,
                  ::testing::VariantWith<
                      opentelemetry::sdk::metrics::HistogramPointData>(
                      ::testing::Field(&opentelemetry::sdk::metrics::
                                           HistogramPointData::count_,
                                       ::testing::Eq(1)))))))));
  data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); },
      reader.get());
  EXPECT_THAT(
      data,
      ::testing::ElementsAre(::testing::Pair(
          kMetricName,
          ::testing::ElementsAre(::testing::AllOf(
              AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                           kOptionalLabelValues),
              ::testing::Field(
                  &opentelemetry::sdk::metrics::PointDataAttributes::point_data,
                  ::testing::VariantWith<
                      opentelemetry::sdk::metrics::HistogramPointData>(
                      ::testing::Field(&opentelemetry::sdk::metrics::
                                           HistogramPointData::count_,
                                       ::testing::Eq(1)))))))));
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
