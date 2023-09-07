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

#include "absl/functional/any_invocable.h"
#include "api/include/opentelemetry/metrics/provider.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"

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

class OTelPluginEnd2EndTest : public ::testing::Test {
 protected:
  // Note that we can't use SetUp() here since we want to send in parameters.
  void Init(
      const absl::flat_hash_set<absl::string_view>& metric_names,
      opentelemetry::sdk::resource::Resource resource =
          opentelemetry::sdk::resource::Resource::Create({}),
      std::unique_ptr<grpc::internal::LabelsInjector> labels_injector = nullptr,
      bool test_no_meter_provider = false,
      absl::AnyInvocable<bool(absl::string_view /*target*/) const>
          target_attribute_filter =
              absl::AnyInvocable<bool(absl::string_view) const>());

  void TearDown() override;

  void ResetStub(std::shared_ptr<Channel> channel);

  void SendRPC();

  absl::flat_hash_map<
      std::string,
      std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
  ReadCurrentMetricsData(
      absl::AnyInvocable<
          bool(const absl::flat_hash_map<
               std::string,
               std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&)>
          continue_predicate);

  const absl::string_view kMethodName = "grpc.testing.EchoTestService/Echo";
  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader_;
  std::string server_address_;
  std::string canonical_server_address_;
  CallbackTestServiceImpl service_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<EchoTestService::Stub> stub_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_EXT_OTEL_OTEL_TEST_LIBRARY_H
