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

#include <type_traits>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
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

template <typename T>
void PopulateLabelMap(
    T label_keys, T label_values,
    std::unordered_map<std::string,
                       opentelemetry::sdk::common::OwnedAttributeValue>*
        label_maps) {
  for (size_t i = 0; i < label_keys.size(); ++i) {
    (*label_maps)[std::string(label_keys[i])] = std::string(label_values[i]);
  }
}

MATCHER_P4(AttributesEq, label_keys, label_values, optional_label_keys,
           optional_label_values, "") {
  std::unordered_map<std::string,
                     opentelemetry::sdk::common::OwnedAttributeValue>
      label_map;
  PopulateLabelMap(label_keys, label_values, &label_map);
  PopulateLabelMap(optional_label_keys, optional_label_values, &label_map);
  return ::testing::ExplainMatchResult(
      ::testing::UnorderedElementsAreArray(label_map),
      arg.attributes.GetAttributes(), result_listener);
}

template <typename T>
auto IntOrDoubleEq(T result) {
  return ::testing::Eq(result);
}
template <>
auto IntOrDoubleEq(double result) {
  return ::testing::DoubleEq(result);
}

MATCHER_P(CounterResultEq, result, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<opentelemetry::sdk::metrics::SumPointData>(
          ::testing::Field(
              &opentelemetry::sdk::metrics::SumPointData::value_,
              ::testing::VariantWith<std::remove_cv_t<decltype(result)>>(
                  IntOrDoubleEq(result)))),
      arg.point_data, result_listener);
}

MATCHER_P4(HistogramResultEq, sum, min, max, count, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<opentelemetry::sdk::metrics::HistogramPointData>(
          ::testing::AllOf(
              ::testing::Field(
                  &opentelemetry::sdk::metrics::HistogramPointData::sum_,
                  ::testing::VariantWith<std::remove_cv_t<decltype(sum)>>(
                      IntOrDoubleEq(sum))),
              ::testing::Field(
                  &opentelemetry::sdk::metrics::HistogramPointData::min_,
                  ::testing::VariantWith<std::remove_cv_t<decltype(min)>>(
                      IntOrDoubleEq(min))),
              ::testing::Field(
                  &opentelemetry::sdk::metrics::HistogramPointData::max_,
                  ::testing::VariantWith<std::remove_cv_t<decltype(max)>>(
                      IntOrDoubleEq(max))),
              ::testing::Field(
                  &opentelemetry::sdk::metrics::HistogramPointData::count_,
                  ::testing::Eq(count)))),
      arg.point_data, result_listener);
}

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

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordUInt64Counter) {
  constexpr absl::string_view kMetricName = "uint64_counter";
  constexpr int kCounterValues[] = {1, 2, 3};
  constexpr int64_t kCounterResult = 6;
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle = grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
      kMetricName, "A simple uint64 counter.", "unit", kLabelKeys,
      kOptionalLabelKeys, /*enable_by_default=*/true);
  Init(std::move(Options()
                     .set_metric_names({kMetricName})
                     .set_target_selector([](absl::string_view target) {
                       return absl::StartsWith(target, "dns:///");
                     })
                     .add_optional_label(kOptionalLabelKeys[0])
                     .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::StatsPlugin::ChannelScope("dns:///localhost:8080", ""));
  for (auto v : kCounterValues) {
    stats_plugins.AddCounter(handle, v, kLabelValues, kOptionalLabelValues);
  }
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(data, ::testing::ElementsAre(::testing::Pair(
                        kMetricName, ::testing::ElementsAre(::testing::AllOf(
                                         AttributesEq(kLabelKeys, kLabelValues,
                                                      kOptionalLabelKeys,
                                                      kOptionalLabelValues),
                                         CounterResultEq(kCounterResult))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordDoubleCounter) {
  constexpr absl::string_view kMetricName = "double_counter";
  constexpr double kCounterValues[] = {1.23, 2.34, 3.45};
  constexpr double kCounterResult = 7.02;
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle = grpc_core::GlobalInstrumentsRegistry::RegisterDoubleCounter(
      kMetricName, "A simple double counter.", "unit", kLabelKeys,
      kOptionalLabelKeys, /*enable_by_default=*/false);
  Init(std::move(Options()
                     .set_metric_names({kMetricName})
                     .set_target_selector([](absl::string_view target) {
                       return absl::StartsWith(target, "dns:///");
                     })
                     .add_optional_label(kOptionalLabelKeys[0])
                     .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::StatsPlugin::ChannelScope("dns:///localhost:8080", ""));
  for (auto v : kCounterValues) {
    stats_plugins.AddCounter(handle, v, kLabelValues, kOptionalLabelValues);
  }
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(data, ::testing::ElementsAre(::testing::Pair(
                        kMetricName, ::testing::ElementsAre(::testing::AllOf(
                                         AttributesEq(kLabelKeys, kLabelValues,
                                                      kOptionalLabelKeys,
                                                      kOptionalLabelValues),
                                         CounterResultEq(kCounterResult))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordUInt64Histogram) {
  constexpr absl::string_view kMetricName = "uint64_histogram";
  constexpr int kHistogramValues[] = {1, 1, 2, 3, 4, 4, 5, 6};
  constexpr int64_t kSum = 26;
  constexpr int64_t kMin = 1;
  constexpr int64_t kMax = 6;
  constexpr int64_t kCount = 8;
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle = grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Histogram(
      kMetricName, "A simple uint64 histogram.", "unit", kLabelKeys,
      kOptionalLabelKeys, /*enable_by_default=*/true);
  Init(std::move(
      Options()
          .set_metric_names({kMetricName})
          .set_server_selector([](const grpc_core::ChannelArgs& args) {
            return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                   GRPC_ARG_SERVER_SELECTOR_VALUE;
          })
          .add_optional_label(kOptionalLabelKeys[0])
          .add_optional_label(kOptionalLabelKeys[1])));
  grpc_core::ChannelArgs args;
  args = args.Set(GRPC_ARG_SERVER_SELECTOR_KEY, GRPC_ARG_SERVER_SELECTOR_VALUE);
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
  for (auto v : kHistogramValues) {
    stats_plugins.RecordHistogram(handle, v, kLabelValues,
                                  kOptionalLabelValues);
  }
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(data,
              ::testing::ElementsAre(::testing::Pair(
                  kMetricName,
                  ::testing::ElementsAre(::testing::AllOf(
                      AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                   kOptionalLabelValues),
                      HistogramResultEq(kSum, kMin, kMax, kCount))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordDoubleHistogram) {
  constexpr absl::string_view kMetricName = "double_histogram";
  constexpr double kHistogramValues[] = {1.1, 1.2, 2.2, 3.3,
                                         4.4, 4.5, 5.5, 6.6};
  constexpr double kSum = 28.8;
  constexpr double kMin = 1.1;
  constexpr double kMax = 6.6;
  constexpr double kCount = 8;
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle = grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
      kMetricName, "A simple double histogram.", "unit", kLabelKeys,
      kOptionalLabelKeys, /*enable_by_default=*/true);
  Init(std::move(
      Options()
          .set_metric_names({kMetricName})
          .set_server_selector([](const grpc_core::ChannelArgs& args) {
            return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                   GRPC_ARG_SERVER_SELECTOR_VALUE;
          })
          .add_optional_label(kOptionalLabelKeys[0])
          .add_optional_label(kOptionalLabelKeys[1])));
  grpc_core::ChannelArgs args;
  args = args.Set(GRPC_ARG_SERVER_SELECTOR_KEY, GRPC_ARG_SERVER_SELECTOR_VALUE);
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
  for (auto v : kHistogramValues) {
    stats_plugins.RecordHistogram(handle, v, kLabelValues,
                                  kOptionalLabelValues);
  }
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(data,
              ::testing::ElementsAre(::testing::Pair(
                  kMetricName,
                  ::testing::ElementsAre(::testing::AllOf(
                      AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                   kOptionalLabelValues),
                      HistogramResultEq(kSum, kMin, kMax, kCount))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest,
       RegisterMultipleOpenTelemetryPlugins) {
  constexpr absl::string_view kMetricName = "yet_another_double_histogram";
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle = grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
      kMetricName, "A simple double histogram.", "unit", kLabelKeys,
      kOptionalLabelKeys, /*enable_by_default=*/true);
  // Build and register a separate OpenTelemetryPlugin and verify its histogram
  // recording.
  grpc::internal::OpenTelemetryPluginBuilderImpl ot_builder;
  auto reader = BuildAndRegisterOpenTelemetryPlugin(std::move(
      Options()
          .set_metric_names({kMetricName})
          .set_server_selector([](const grpc_core::ChannelArgs& args) {
            return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                   GRPC_ARG_SERVER_SELECTOR_VALUE;
          })
          .add_optional_label(kOptionalLabelKeys[0])
          .add_optional_label(kOptionalLabelKeys[1])));
  EXPECT_EQ(ot_builder.BuildAndRegisterGlobal(), absl::OkStatus());
  grpc_core::ChannelArgs args;
  args = args.Set(GRPC_ARG_SERVER_SELECTOR_KEY, GRPC_ARG_SERVER_SELECTOR_VALUE);
  {
    constexpr double kHistogramValues[] = {1.23, 2.34, 3.45, 4.56};
    constexpr double kSum = 11.58;
    constexpr double kMin = 1.23;
    constexpr double kMax = 4.56;
    constexpr int kCount = 4;
    auto stats_plugins =
        grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
    for (auto v : kHistogramValues) {
      stats_plugins.RecordHistogram(handle, v, kLabelValues,
                                    kOptionalLabelValues);
    }
    auto data = ReadCurrentMetricsData(
        [&](const absl::flat_hash_map<
            std::string,
            std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                data) { return !data.contains(kMetricName); },
        reader.get());
    EXPECT_THAT(
        data, ::testing::ElementsAre(::testing::Pair(
                  kMetricName,
                  ::testing::ElementsAre(::testing::AllOf(
                      AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                   kOptionalLabelValues),
                      HistogramResultEq(kSum, kMin, kMax, kCount))))));
  }
  // Now build and register another OpenTelemetryPlugin using the test fixture
  // and record histogram.
  constexpr double kHistogramValues[] = {1.1, 1.2, 2.2, 3.3,
                                         4.4, 4.5, 5.5, 6.6};
  constexpr double kSum = 28.8;
  constexpr double kMin = 1.1;
  constexpr double kMax = 6.6;
  constexpr int kCount = 8;
  Init(std::move(
      Options()
          .set_metric_names({kMetricName})
          .set_server_selector([](const grpc_core::ChannelArgs& args) {
            return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                   GRPC_ARG_SERVER_SELECTOR_VALUE;
          })
          .add_optional_label(kOptionalLabelKeys[0])
          .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
  for (auto v : kHistogramValues) {
    stats_plugins.RecordHistogram(handle, v, kLabelValues,
                                  kOptionalLabelValues);
  }
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(data,
              ::testing::ElementsAre(::testing::Pair(
                  kMetricName,
                  ::testing::ElementsAre(::testing::AllOf(
                      AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                   kOptionalLabelValues),
                      HistogramResultEq(kSum, kMin, kMax, kCount))))));
  // Verify that the first plugin gets the data as well.
  data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); },
      reader.get());
  EXPECT_THAT(data,
              ::testing::ElementsAre(::testing::Pair(
                  kMetricName,
                  ::testing::ElementsAre(::testing::AllOf(
                      AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                   kOptionalLabelValues),
                      HistogramResultEq(kSum, kMin, kMax, kCount))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest,
       DisabledOptionalLabelKeysShouldNotBeRecorded) {
  constexpr absl::string_view kMetricName =
      "yet_another_yet_another_double_histogram";
  constexpr double kHistogramValues[] = {1.1, 1.2, 2.2, 3.3,
                                         4.4, 4.5, 5.5, 6.6};
  constexpr double kSum = 28.8;
  constexpr double kMin = 1.1;
  constexpr double kMax = 6.6;
  constexpr double kCount = 8;
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 4> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2", "optional_label_key_3",
      "optional_label_key_4"};
  constexpr std::array<absl::string_view, 2> kActualOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 4> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2",
      "optional_label_value_3", "optional_label_value_4"};
  constexpr std::array<absl::string_view, 2> kActualOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle = grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
      kMetricName, "A simple double histogram.", "unit", kLabelKeys,
      kOptionalLabelKeys, /*enable_by_default=*/true);
  Init(std::move(
      Options()
          .set_metric_names({kMetricName})
          .set_server_selector([](const grpc_core::ChannelArgs& args) {
            return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                   GRPC_ARG_SERVER_SELECTOR_VALUE;
          })
          .add_optional_label(kOptionalLabelKeys[0])
          .add_optional_label(kOptionalLabelKeys[1])));
  grpc_core::ChannelArgs args;
  args = args.Set(GRPC_ARG_SERVER_SELECTOR_KEY, GRPC_ARG_SERVER_SELECTOR_VALUE);
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
  for (auto v : kHistogramValues) {
    stats_plugins.RecordHistogram(handle, v, kLabelValues,
                                  kOptionalLabelValues);
  }
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
              AttributesEq(kLabelKeys, kLabelValues, kActualOptionalLabelKeys,
                           kActualOptionalLabelValues),
              HistogramResultEq(kSum, kMin, kMax, kCount))))));
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

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
