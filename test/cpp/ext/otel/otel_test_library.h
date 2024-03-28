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

#ifndef GRPC_TEST_CPP_EXT_OTEL_OTEL_TEST_LIBRARY_H
#define GRPC_TEST_CPP_EXT_OTEL_OTEL_TEST_LIBRARY_H

#include <grpc/support/port_platform.h>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"

#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {

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

class OpenTelemetryPluginEnd2EndTest : public ::testing::Test {
 protected:
  struct Options {
   public:
    Options& set_metric_names(std::vector<absl::string_view> names) {
      metric_names = std::move(names);
      return *this;
    }

    Options& set_resource(const opentelemetry::sdk::resource::Resource& res) {
      resource = std::make_unique<opentelemetry::sdk::resource::Resource>(res);
      return *this;
    }

    Options& set_use_meter_provider(bool flag) {
      use_meter_provider = flag;
      return *this;
    }

    Options& set_labels_to_inject(std::map<std::string, std::string> labels) {
      labels_to_inject = std::move(labels);
      return *this;
    }

    Options& set_target_selector(
        absl::AnyInvocable<bool(absl::string_view /*target*/) const> func) {
      target_selector = std::move(func);
      return *this;
    }

    Options& set_server_selector(
        absl::AnyInvocable<bool(const grpc_core::ChannelArgs& /*channel_args*/)
                               const>
            func) {
      server_selector = std::move(func);
      return *this;
    }

    Options& set_target_attribute_filter(
        absl::AnyInvocable<bool(absl::string_view /*target*/) const> func) {
      target_attribute_filter = std::move(func);
      return *this;
    }

    Options& set_generic_method_attribute_filter(
        absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
            func) {
      generic_method_attribute_filter = std::move(func);
      return *this;
    }

    Options& add_plugin_option(
        std::unique_ptr<grpc::internal::InternalOpenTelemetryPluginOption>
            option) {
      plugin_options.push_back(std::move(option));
      return *this;
    }

    Options& add_optional_label(absl::string_view optional_label_key) {
      optional_label_keys.emplace(optional_label_key);
      return *this;
    }

    std::vector<absl::string_view> metric_names;
    // TODO(yashykt): opentelemetry::sdk::resource::Resource doesn't have a copy
    // assignment operator so wrapping it in a unique_ptr till it is fixed.
    std::unique_ptr<opentelemetry::sdk::resource::Resource> resource =
        std::make_unique<opentelemetry::sdk::resource::Resource>(
            opentelemetry::sdk::resource::Resource::Create({}));
    std::unique_ptr<grpc::internal::LabelsInjector> labels_injector;
    bool use_meter_provider = true;
    std::map<std::string, std::string> labels_to_inject;
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_selector;
    absl::AnyInvocable<bool(const grpc_core::ChannelArgs& /*channel_args*/)
                           const>
        server_selector;
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_attribute_filter;
    absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
        generic_method_attribute_filter;
    std::vector<
        std::unique_ptr<grpc::internal::InternalOpenTelemetryPluginOption>>
        plugin_options;
    absl::flat_hash_set<absl::string_view> optional_label_keys;
  };

  // Note that we can't use SetUp() here since we want to send in parameters.
  void Init(Options config);

  void TearDown() override;

  void ResetStub(std::shared_ptr<Channel> channel);

  void SendRPC();
  void SendGenericRPC();

  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader>
  BuildAndRegisterOpenTelemetryPlugin(
      OpenTelemetryPluginEnd2EndTest::Options options);

  absl::flat_hash_map<
      std::string,
      std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
  ReadCurrentMetricsData(
      absl::AnyInvocable<
          bool(const absl::flat_hash_map<
               std::string,
               std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&)>
          continue_predicate,
      opentelemetry::sdk::metrics::MetricReader* reader = nullptr);

  const absl::string_view kMethodName = "grpc.testing.EchoTestService/Echo";
  const absl::string_view kGenericMethodName = "foo/bar";
  std::map<std::string, std::string> labels_to_inject_;
  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader_;
  std::string server_address_;
  std::string canonical_server_address_;
  CallbackTestServiceImpl service_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<EchoTestService::Stub> stub_;
  std::unique_ptr<grpc::GenericStub> generic_stub_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_EXT_OTEL_OTEL_TEST_LIBRARY_H
