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
struct Extract;

template <template <typename> class T, typename U>
struct Extract<const T<U>> {
  using Type = U;
};

MATCHER_P(CounterResultEq, value_matcher, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<opentelemetry::sdk::metrics::SumPointData>(
          ::testing::Field(&opentelemetry::sdk::metrics::SumPointData::value_,
                           ::testing::VariantWith<
                               typename Extract<decltype(value_matcher)>::Type>(
                               value_matcher))),
      arg.point_data, result_listener);
}

MATCHER_P4(HistogramResultEq, sum_matcher, min_matcher, max_matcher, count,
           "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<
          opentelemetry::sdk::metrics::HistogramPointData>(::testing::AllOf(
          ::testing::Field(
              &opentelemetry::sdk::metrics::HistogramPointData::sum_,
              ::testing::VariantWith<
                  typename Extract<decltype(sum_matcher)>::Type>(sum_matcher)),
          ::testing::Field(
              &opentelemetry::sdk::metrics::HistogramPointData::min_,
              ::testing::VariantWith<
                  typename Extract<decltype(min_matcher)>::Type>(min_matcher)),
          ::testing::Field(
              &opentelemetry::sdk::metrics::HistogramPointData::max_,
              ::testing::VariantWith<
                  typename Extract<decltype(max_matcher)>::Type>(max_matcher)),
          ::testing::Field(
              &opentelemetry::sdk::metrics::HistogramPointData::count_,
              ::testing::Eq(count)))),
      arg.point_data, result_listener);
}

MATCHER_P(GaugeResultIs, value_matcher, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<opentelemetry::sdk::metrics::LastValuePointData>(
          ::testing::AllOf(
              ::testing::Field(
                  &opentelemetry::sdk::metrics::LastValuePointData::value_,
                  ::testing::VariantWith<
                      typename Extract<decltype(value_matcher)>::Type>(
                      value_matcher)),
              ::testing::Field(&opentelemetry::sdk::metrics::
                                   LastValuePointData::is_lastvalue_valid_,
                               ::testing::IsTrue()))),
      arg.point_data, result_listener);
}

// This check might subject to system clock adjustment.
MATCHER_P(GaugeResultLaterThan, prev_timestamp, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<opentelemetry::sdk::metrics::LastValuePointData>(
          ::testing::Field(
              &opentelemetry::sdk::metrics::LastValuePointData::sample_ts_,
              ::testing::Property(
                  &opentelemetry::common::SystemTimestamp::time_since_epoch,
                  ::testing::Gt(prev_timestamp.time_since_epoch())))),
      arg.point_data, result_listener);
}

MATCHER_P7(GaugeDataIsIncrementalForSpecificMetricAndLabelSet, metric_name,
           label_key, label_value, optional_label_key, optional_label_value,
           default_value, greater_than, "") {
  std::unordered_map<std::string,
                     opentelemetry::sdk::common::OwnedAttributeValue>
      label_map;
  PopulateLabelMap(label_key, label_value, &label_map);
  PopulateLabelMap(optional_label_key, optional_label_value, &label_map);
  opentelemetry::common::SystemTimestamp prev_timestamp;
  auto prev_value = default_value;
  size_t prev_index = 0;
  auto& data = arg.at(metric_name);
  bool result = true;
  for (size_t i = 1; i < data.size(); ++i) {
    if (::testing::Matches(::testing::UnorderedElementsAreArray(
            data[i - 1].attributes.GetAttributes()))(label_map)) {
      // Update the previous value for the same associated label values.
      prev_value = opentelemetry::nostd::get<decltype(prev_value)>(
          opentelemetry::nostd::get<
              opentelemetry::sdk::metrics::LastValuePointData>(
              data[i - 1].point_data)
              .value_);
      prev_index = i - 1;
      prev_timestamp = opentelemetry::nostd::get<
                           opentelemetry::sdk::metrics::LastValuePointData>(
                           data[i - 1].point_data)
                           .sample_ts_;
    }
    if (!::testing::Matches(::testing::UnorderedElementsAreArray(
            data[i].attributes.GetAttributes()))(label_map)) {
      // Skip values that do not have the same associated label values.
      continue;
    }
    *result_listener << " Comparing data[" << i << "] with data[" << prev_index
                     << "] ";
    if (greater_than) {
      result &= ::testing::ExplainMatchResult(
          ::testing::AllOf(
              AttributesEq(label_key, label_value, optional_label_key,
                           optional_label_value),
              GaugeResultIs(::testing::Gt(prev_value)),
              GaugeResultLaterThan(prev_timestamp)),
          data[i], result_listener);
    } else {
      result &= ::testing::ExplainMatchResult(
          ::testing::AllOf(
              AttributesEq(label_key, label_value, optional_label_key,
                           optional_label_value),
              GaugeResultIs(::testing::Ge(prev_value)),
              GaugeResultLaterThan(prev_timestamp)),
          data[i], result_listener);
    }
  }
  return result;
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

using OpenTelemetryPluginOptionEnd2EndTest = OpenTelemetryPluginEnd2EndTest;

class SimpleLabelIterable : public grpc::internal::LabelsIterable {
 public:
  explicit SimpleLabelIterable(
      std::pair<absl::string_view, absl::string_view> label)
      : label_(label) {}

  std::optional<std::pair<absl::string_view, absl::string_view>> Next()
      override {
    if (iterated_) {
      return std::nullopt;
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

  bool AddOptionalLabels(bool /*is_client*/,
                         absl::Span<const grpc_core::RefCountedStringValue>
                         /*optional_labels*/,
                         opentelemetry::nostd::function_ref<
                             bool(opentelemetry::nostd::string_view,
                                  opentelemetry::common::AttributeValue)>
                         /*callback*/) const override {
    return true;
  }

  size_t GetOptionalLabelsSize(
      bool /*is_client*/, absl::Span<const grpc_core::RefCountedStringValue>
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
                        std::pair("key", "value")))));
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
  EXPECT_EQ(std::get<std::string>(client_attributes.at("key")), "value");
  // Verify server side metric
  ASSERT_EQ(data["grpc.server.call.duration"].size(), 1);
  const auto& server_attributes =
      data["grpc.server.call.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(server_attributes.size(), 3);
  EXPECT_EQ(std::get<std::string>(server_attributes.at("key")), "value");
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
                        std::pair("key", "value")))));
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
  EXPECT_EQ(std::get<std::string>(client_attributes.at("key")), "value");
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
                        std::pair("key", "value")))));
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
  EXPECT_EQ(std::get<std::string>(server_attributes.at("key")), "value");
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
                        std::pair("key1", "value1")))
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ true, /*enabled_on_server*/ false,
                        std::pair("key2", "value2")))
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ true, /*enabled_on_server*/ false,
                        std::pair("key3", "value3")))
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ false, /*enabled_on_server*/ true,
                        std::pair("key4", "value4")))
                    .add_plugin_option(std::make_unique<CustomPluginOption>(
                        /*enabled_on_client*/ false, /*enabled_on_server*/ true,
                        std::pair("key5", "value5")))));
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
  EXPECT_EQ(std::get<std::string>(client_attributes.at("key1")), "value1");
  EXPECT_EQ(std::get<std::string>(client_attributes.at("key2")), "value2");
  EXPECT_EQ(std::get<std::string>(client_attributes.at("key3")), "value3");
  EXPECT_THAT(client_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("key4"))));
  EXPECT_THAT(client_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("key5"))));
  // Verify server side metric
  ASSERT_EQ(data["grpc.server.call.duration"].size(), 1);
  const auto& server_attributes =
      data["grpc.server.call.duration"][0].attributes.GetAttributes();
  EXPECT_EQ(server_attributes.size(), 5);
  EXPECT_EQ(std::get<std::string>(server_attributes.at("key1")), "value1");
  EXPECT_THAT(server_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("key2"))));
  EXPECT_THAT(server_attributes,
              ::testing::Not(::testing::Contains(::testing::Key("key3"))));
  EXPECT_EQ(std::get<std::string>(server_attributes.at("key4")), "value4");
  EXPECT_EQ(std::get<std::string>(server_attributes.at("key5")), "value5");
}

class OpenTelemetryPluginNPCMetricsTest
    : public OpenTelemetryPluginEnd2EndTest {
 protected:
  OpenTelemetryPluginNPCMetricsTest()
      : endpoint_config_(grpc_core::ChannelArgs()) {}

  void TearDown() override {
    // We are tearing down OpenTelemetryPluginEnd2EndTest first to ensure that
    // gRPC has shutdown before we reset the instruments registry.
    OpenTelemetryPluginEnd2EndTest::TearDown();
    grpc_core::GlobalInstrumentsRegistryTestPeer::
        ResetGlobalInstrumentsRegistry();
  }

  grpc_event_engine::experimental::ChannelArgsEndpointConfig endpoint_config_;
};

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordUInt64Counter) {
  constexpr absl::string_view kMetricName = "uint64_counter";
  constexpr uint64_t kCounterValues[] = {1, 2, 3};
  constexpr int64_t kCounterResult = 6;
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
          kMetricName, "A simple uint64 counter.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  Init(std::move(Options()
                     .set_metric_names({kMetricName})
                     .set_channel_scope_filter(
                         [](const OpenTelemetryPluginBuilder::ChannelScope&
                                channel_scope) {
                           return absl::StartsWith(channel_scope.target(),
                                                   "dns:///");
                         })
                     .add_optional_label(kOptionalLabelKeys[0])
                     .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  for (auto v : kCounterValues) {
    stats_plugins.AddCounter(handle, v, kLabelValues, kOptionalLabelValues);
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
                      CounterResultEq(::testing::Eq(kCounterResult)))))));
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
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterDoubleCounter(
          kMetricName, "A simple double counter.", "unit",
          /*enable_by_default=*/false)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  Init(std::move(Options()
                     .set_metric_names({kMetricName})
                     .set_channel_scope_filter(
                         [](const OpenTelemetryPluginBuilder::ChannelScope&
                                channel_scope) {
                           return absl::StartsWith(channel_scope.target(),
                                                   "dns:///");
                         })
                     .add_optional_label(kOptionalLabelKeys[0])
                     .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  for (auto v : kCounterValues) {
    stats_plugins.AddCounter(handle, v, kLabelValues, kOptionalLabelValues);
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
                      CounterResultEq(::testing::DoubleEq(kCounterResult)))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordUInt64Histogram) {
  constexpr absl::string_view kMetricName = "uint64_histogram";
  constexpr uint64_t kHistogramValues[] = {1, 1, 2, 3, 4, 4, 5, 6};
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
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Histogram(
          kMetricName, "A simple uint64 histogram.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
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
      data, ::testing::ElementsAre(::testing::Pair(
                kMetricName,
                ::testing::ElementsAre(::testing::AllOf(
                    AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                 kOptionalLabelValues),
                    HistogramResultEq(::testing::Eq(kSum), ::testing::Eq(kMin),
                                      ::testing::Eq(kMax), kCount))))));
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
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          kMetricName, "A simple double histogram.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
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
                      HistogramResultEq(::testing::DoubleEq(kSum),
                                        ::testing::DoubleEq(kMin),
                                        ::testing::DoubleEq(kMax), kCount))))));
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
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          kMetricName, "A simple double histogram.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  // Build and register a separate OpenTelemetryPlugin and verify its histogram
  // recording.
  auto reader = BuildAndRegisterOpenTelemetryPlugin(std::move(
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
                      HistogramResultEq(::testing::DoubleEq(kSum),
                                        ::testing::DoubleEq(kMin),
                                        ::testing::DoubleEq(kMax), kCount))))));
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
                      HistogramResultEq(::testing::DoubleEq(kSum),
                                        ::testing::DoubleEq(kMin),
                                        ::testing::DoubleEq(kMax), kCount))))));
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
                      HistogramResultEq(::testing::DoubleEq(kSum),
                                        ::testing::DoubleEq(kMin),
                                        ::testing::DoubleEq(kMax), kCount))))));
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
  constexpr std::array<absl::string_view, 3> kActualOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2", "optional_label_key_4"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 4> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2",
      "optional_label_value_3", "optional_label_value_4"};
  constexpr std::array<absl::string_view, 3> kActualOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2",
      "optional_label_value_4"};
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          kMetricName, "A simple double histogram.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1],
                          kOptionalLabelKeys[2], kOptionalLabelKeys[3])
          .Build();
  Init(std::move(
      Options()
          .set_metric_names({kMetricName})
          .set_server_selector([](const grpc_core::ChannelArgs& args) {
            return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                   GRPC_ARG_SERVER_SELECTOR_VALUE;
          })
          .add_optional_label(kOptionalLabelKeys[0])
          .add_optional_label(kOptionalLabelKeys[1])
          .add_optional_label(kOptionalLabelKeys[3])));
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
              HistogramResultEq(::testing::DoubleEq(kSum),
                                ::testing::DoubleEq(kMin),
                                ::testing::DoubleEq(kMax), kCount))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, InstrumentsEnabledTest) {
  constexpr absl::string_view kDoubleHistogramMetricName =
      "yet_another_yet_another_double_histogram";
  constexpr absl::string_view kUnit64CounterMetricName = "uint64_counter";
  auto histogram_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          kDoubleHistogramMetricName, "A simple double histogram.", "unit",
          /*enable_by_default=*/false)
          .Build();
  auto counter_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
          kUnit64CounterMetricName, "A simple unit64 counter.", "unit",
          /*enable_by_default=*/false)
          .Build();
  Init(std::move(Options().set_metric_names({kDoubleHistogramMetricName})));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(
          grpc_core::ChannelArgs());
  EXPECT_TRUE(stats_plugins.IsInstrumentEnabled(histogram_handle));
  EXPECT_FALSE(stats_plugins.IsInstrumentEnabled(counter_handle));
}

using OpenTelemetryPluginCallbackMetricsTest =
    OpenTelemetryPluginNPCMetricsTest;

// The callback minimal interval is longer than the OT reporting interval, so we
// expect to collect duplicated (cached) values.
TEST_F(OpenTelemetryPluginCallbackMetricsTest,
       ReportDurationLongerThanCollectDuration) {
  constexpr absl::string_view kInt64CallbackGaugeMetric =
      "int64_callback_gauge";
  constexpr absl::string_view kDoubleCallbackGaugeMetric =
      "double_callback_gauge";
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValuesSet1 = {
      "label_value_set_1", "label_value_set_1"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValuesSet1 = {
      "optional_label_value_set_1", "optional_label_value_set_1"};
  constexpr std::array<absl::string_view, 2> kLabelValuesSet2 = {
      "label_value_set_2", "label_value_set_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValuesSet2 = {
      "optional_label_value_set_2", "optional_label_value_set_2"};
  auto integer_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackInt64Gauge(
          kInt64CallbackGaugeMetric, "An int64 callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  auto double_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackDoubleGauge(
          kDoubleCallbackGaugeMetric, "A double callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  Init(std::move(Options()
                     .set_metric_names({kInt64CallbackGaugeMetric,
                                        kDoubleCallbackGaugeMetric})
                     .add_optional_label(kOptionalLabelKeys[0])
                     .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  // Multiple callbacks for the same metrics, each reporting different
  // label values.
  int report_count_1 = 0;
  int64_t int_value_1 = 1;
  double double_value_1 = 0.5;
  auto registered_metric_callback_1 = stats_plugins.RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_1;
        reporter.Report(integer_gauge_handle, int_value_1++, kLabelValuesSet1,
                        kOptionalLabelValuesSet1);
        reporter.Report(double_gauge_handle, double_value_1++, kLabelValuesSet1,
                        kOptionalLabelValuesSet1);
        ;
      },
      grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  int report_count_2 = 0;
  int64_t int_value_2 = 1;
  double double_value_2 = 0.5;
  auto registered_metric_callback_2 = stats_plugins.RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_2;
        reporter.Report(integer_gauge_handle, int_value_2++, kLabelValuesSet2,
                        kOptionalLabelValuesSet2);
        reporter.Report(double_gauge_handle, double_value_2++, kLabelValuesSet2,
                        kOptionalLabelValuesSet2);
      },
      grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  constexpr int kIterations = 100;
  MetricsCollectorThread collector{
      this, grpc_core::Duration::Milliseconds(10) * grpc_test_slowdown_factor(),
      kIterations,
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) {
        return !data.contains(kInt64CallbackGaugeMetric) ||
               !data.contains(kDoubleCallbackGaugeMetric);
      }};
  absl::flat_hash_map<
      std::string,
      std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
      data = collector.Stop();
  // Verify that data is incremental with duplications (cached values).
  EXPECT_LT(report_count_1, kIterations);
  EXPECT_LT(report_count_2, kIterations);
  EXPECT_EQ(data[kInt64CallbackGaugeMetric].size(),
            data[kDoubleCallbackGaugeMetric].size());
  // Verify labels.
  ASSERT_THAT(
      data,
      ::testing::UnorderedElementsAre(
          ::testing::Pair(
              kInt64CallbackGaugeMetric,
              ::testing::Each(::testing::AnyOf(
                  AttributesEq(kLabelKeys, kLabelValuesSet1, kOptionalLabelKeys,
                               kOptionalLabelValuesSet1),
                  AttributesEq(kLabelKeys, kLabelValuesSet2, kOptionalLabelKeys,
                               kOptionalLabelValuesSet2)))),
          ::testing::Pair(
              kDoubleCallbackGaugeMetric,
              ::testing::Each(::testing::AnyOf(
                  AttributesEq(kLabelKeys, kLabelValuesSet1, kOptionalLabelKeys,
                               kOptionalLabelValuesSet1),
                  AttributesEq(kLabelKeys, kLabelValuesSet2, kOptionalLabelKeys,
                               kOptionalLabelValuesSet2))))));
  EXPECT_THAT(data, GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                        kInt64CallbackGaugeMetric, kLabelKeys, kLabelValuesSet1,
                        kOptionalLabelKeys, kOptionalLabelValuesSet1,
                        int64_t(0), false));
  EXPECT_THAT(data, GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                        kInt64CallbackGaugeMetric, kLabelKeys, kLabelValuesSet2,
                        kOptionalLabelKeys, kOptionalLabelValuesSet2,
                        int64_t(0), false));
  EXPECT_THAT(data,
              GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                  kDoubleCallbackGaugeMetric, kLabelKeys, kLabelValuesSet1,
                  kOptionalLabelKeys, kOptionalLabelValuesSet1, 0.0, false));
  EXPECT_THAT(data,
              GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                  kDoubleCallbackGaugeMetric, kLabelKeys, kLabelValuesSet2,
                  kOptionalLabelKeys, kOptionalLabelValuesSet2, 0.0, false));
}

// The callback minimal interval is shorter than the OT reporting interval, so
// for each collect we should go update the cache and report the latest values.
TEST_F(OpenTelemetryPluginCallbackMetricsTest,
       ReportDurationShorterThanCollectDuration) {
  constexpr absl::string_view kInt64CallbackGaugeMetric =
      "yet_another_int64_callback_gauge";
  constexpr absl::string_view kDoubleCallbackGaugeMetric =
      "yet_another_double_callback_gauge";
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValuesSet1 = {
      "label_value_set_1", "label_value_set_1"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValuesSet1 = {
      "optional_label_value_set_1", "optional_label_value_set_1"};
  constexpr std::array<absl::string_view, 2> kLabelValuesSet2 = {
      "label_value_set_2", "label_value_set_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValuesSet2 = {
      "optional_label_value_set_2", "optional_label_value_set_2"};
  auto integer_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackInt64Gauge(
          kInt64CallbackGaugeMetric, "An int64 callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  auto double_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackDoubleGauge(
          kDoubleCallbackGaugeMetric, "A double callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  Init(std::move(Options()
                     .set_metric_names({kInt64CallbackGaugeMetric,
                                        kDoubleCallbackGaugeMetric})
                     .add_optional_label(kOptionalLabelKeys[0])
                     .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  // Multiple callbacks for the same metrics, each reporting different
  // label values.
  int report_count_1 = 0;
  int64_t int_value_1 = 1;
  double double_value_1 = 0.5;
  auto registered_metric_callback_1 = stats_plugins.RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_1;
        reporter.Report(integer_gauge_handle, int_value_1++, kLabelValuesSet1,
                        kOptionalLabelValuesSet1);
        reporter.Report(double_gauge_handle, double_value_1++, kLabelValuesSet1,
                        kOptionalLabelValuesSet1);
      },
      grpc_core::Duration::Milliseconds(50) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  int report_count_2 = 0;
  int64_t int_value_2 = 1;
  double double_value_2 = 0.5;
  auto registered_metric_callback_2 = stats_plugins.RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_2;
        reporter.Report(integer_gauge_handle, int_value_2++, kLabelValuesSet2,
                        kOptionalLabelValuesSet2);
        reporter.Report(double_gauge_handle, double_value_2++, kLabelValuesSet2,
                        kOptionalLabelValuesSet2);
      },
      grpc_core::Duration::Milliseconds(50) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  constexpr int kIterations = 50;
  MetricsCollectorThread collector{
      this,
      grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
      kIterations,
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) {
        return !data.contains(kInt64CallbackGaugeMetric) ||
               !data.contains(kDoubleCallbackGaugeMetric);
      }};
  absl::flat_hash_map<
      std::string,
      std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
      data = collector.Stop();
  // Verify that data is incremental without duplications (cached
  // values).
  EXPECT_EQ(report_count_1, kIterations);
  EXPECT_EQ(report_count_2, kIterations);
  EXPECT_EQ(data[kInt64CallbackGaugeMetric].size(),
            data[kDoubleCallbackGaugeMetric].size());
  // Verify labels.
  ASSERT_THAT(
      data,
      ::testing::UnorderedElementsAre(
          ::testing::Pair(
              kInt64CallbackGaugeMetric,
              ::testing::Each(::testing::AnyOf(
                  AttributesEq(kLabelKeys, kLabelValuesSet1, kOptionalLabelKeys,
                               kOptionalLabelValuesSet1),
                  AttributesEq(kLabelKeys, kLabelValuesSet2, kOptionalLabelKeys,
                               kOptionalLabelValuesSet2)))),
          ::testing::Pair(
              kDoubleCallbackGaugeMetric,
              ::testing::Each(::testing::AnyOf(
                  AttributesEq(kLabelKeys, kLabelValuesSet1, kOptionalLabelKeys,
                               kOptionalLabelValuesSet1),
                  AttributesEq(kLabelKeys, kLabelValuesSet2, kOptionalLabelKeys,
                               kOptionalLabelValuesSet2))))));
  EXPECT_THAT(data, GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                        kInt64CallbackGaugeMetric, kLabelKeys, kLabelValuesSet1,
                        kOptionalLabelKeys, kOptionalLabelValuesSet1,
                        int64_t(0), true));
  EXPECT_THAT(data, GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                        kInt64CallbackGaugeMetric, kLabelKeys, kLabelValuesSet2,
                        kOptionalLabelKeys, kOptionalLabelValuesSet2,
                        int64_t(0), true));
  EXPECT_THAT(data,
              GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                  kDoubleCallbackGaugeMetric, kLabelKeys, kLabelValuesSet1,
                  kOptionalLabelKeys, kOptionalLabelValuesSet1, 0.0, true));
  EXPECT_THAT(data,
              GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                  kDoubleCallbackGaugeMetric, kLabelKeys, kLabelValuesSet2,
                  kOptionalLabelKeys, kOptionalLabelValuesSet2, 0.0, true));
}

// Verifies that callbacks are cleaned up when the OpenTelemetry plugin is
// destroyed.
TEST_F(OpenTelemetryPluginCallbackMetricsTest, VerifyCallbacksAreCleanedUp) {
  constexpr absl::string_view kInt64CallbackGaugeMetric =
      "yet_another_int64_callback_gauge";
  constexpr absl::string_view kDoubleCallbackGaugeMetric =
      "yet_another_double_callback_gauge";
  auto integer_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackInt64Gauge(
          kInt64CallbackGaugeMetric, "An int64 callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Build();
  auto double_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackDoubleGauge(
          kDoubleCallbackGaugeMetric, "A double callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Build();
  Init(std::move(Options().set_metric_names(
      {kInt64CallbackGaugeMetric, kDoubleCallbackGaugeMetric})));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  // Multiple callbacks for the same metrics, each reporting different
  // label values.
  int report_count_1 = 0;
  int64_t int_value_1 = 1;
  double double_value_1 = 0.5;
  auto registered_metric_callback_1 = stats_plugins.RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_1;
        reporter.Report(integer_gauge_handle, int_value_1++, {}, {});
        reporter.Report(double_gauge_handle, double_value_1++, {}, {});
      },
      grpc_core::Duration::Milliseconds(50) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  int report_count_2 = 0;
  int64_t int_value_2 = 1;
  double double_value_2 = 0.5;
  auto registered_metric_callback_2 = stats_plugins.RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_2;
        reporter.Report(integer_gauge_handle, int_value_2++, {}, {});
        reporter.Report(double_gauge_handle, double_value_2++, {}, {});
      },
      grpc_core::Duration::Milliseconds(50) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  constexpr int kIterations = 50;
  {
    MetricsCollectorThread collector{
        this,
        grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
        kIterations,
        [&](const absl::flat_hash_map<
            std::string,
            std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                data) {
          return !data.contains(kInt64CallbackGaugeMetric) ||
                 !data.contains(kDoubleCallbackGaugeMetric);
        }};
  }
  // Verify that callbacks are invoked
  EXPECT_EQ(report_count_1, kIterations);
  EXPECT_EQ(report_count_2, kIterations);
  // Remove one of the callbacks
  registered_metric_callback_1.reset();
  {
    MetricsCollectorThread new_collector{
        this,
        grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
        kIterations,
        [&](const absl::flat_hash_map<
            std::string,
            std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                data) { return false; }};
  }
  EXPECT_EQ(report_count_1, kIterations);      // No change since previous
  EXPECT_EQ(report_count_2, 2 * kIterations);  // Gets another kIterations
  // Remove the other callback as well
  registered_metric_callback_2.reset();
  MetricsCollectorThread new_new_collector{
      this,
      grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
      kIterations,
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return false; }};
  // We shouldn't get any new callbacks
  EXPECT_THAT(new_new_collector.Stop(), ::testing::IsEmpty());
  EXPECT_EQ(report_count_1, kIterations);
  EXPECT_EQ(report_count_2, 2 * kIterations);
  // Reset stats plugins as well
  grpc_core::GlobalStatsPluginRegistryTestPeer::
      ResetGlobalStatsPluginRegistry();
  registered_metric_callback_2.reset();
  MetricsCollectorThread new_new_new_collector{
      this,
      grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
      kIterations,
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return false; }};
  // Still no new callbacks
  EXPECT_THAT(new_new_new_collector.Stop(), ::testing::IsEmpty());
  EXPECT_EQ(report_count_1, kIterations);
  EXPECT_EQ(report_count_2, 2 * kIterations);
}

TEST_F(OpenTelemetryPluginCallbackMetricsTest,
       ReportDifferentGaugeThanRegisteredWontCrash) {
  constexpr absl::string_view kInt64CallbackGaugeMetric =
      "yet_another_int64_callback_gauge";
  constexpr absl::string_view kDoubleCallbackGaugeMetric =
      "yet_another_double_callback_gauge";
  auto integer_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackInt64Gauge(
          kInt64CallbackGaugeMetric, "An int64 callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Build();
  auto double_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackDoubleGauge(
          kDoubleCallbackGaugeMetric, "A double callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Build();
  Init(std::move(Options().set_metric_names(
      {kInt64CallbackGaugeMetric, kDoubleCallbackGaugeMetric})));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  // Registers integer_gauge_handle but reports double_gauge_handle.
  int report_count_1 = 0;
  double double_value_1 = 0.5;
  auto registered_metric_callback_1 = stats_plugins.RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_1;
        reporter.Report(double_gauge_handle, double_value_1++, {}, {});
      },
      grpc_core::Duration::Milliseconds(50) * grpc_test_slowdown_factor(),
      integer_gauge_handle);
  constexpr int kIterations = 50;
  {
    MetricsCollectorThread collector{
        this,
        grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
        kIterations,
        [&](const absl::flat_hash_map<
            std::string,
            std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                data) { return false; }};
  }
  // Verify that callbacks are invoked
  EXPECT_EQ(report_count_1, kIterations);
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
  stub->Echo(&context, request, &response);
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
