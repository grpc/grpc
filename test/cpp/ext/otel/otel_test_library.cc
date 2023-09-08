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

#include "test/cpp/ext/otel/otel_test_library.h"

#include "absl/functional/any_invocable.h"
#include "api/include/opentelemetry/metrics/provider.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"

#include <grpcpp/grpcpp.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/config/core_configuration.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {

void OTelPluginEnd2EndTest::Init(
    const absl::flat_hash_set<absl::string_view>& metric_names,
    opentelemetry::sdk::resource::Resource resource,
    std::unique_ptr<grpc::internal::LabelsInjector> labels_injector,
    bool test_no_meter_provider,
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_selector,
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_attribute_filter) {
  // We are resetting the MeterProvider and OpenTelemetry plugin at the start
  // of each test to avoid test results from one test carrying over to another
  // test. (Some measurements can get arbitrarily delayed.)
  auto meter_provider =
      std::make_shared<opentelemetry::sdk::metrics::MeterProvider>(
          std::make_unique<opentelemetry::sdk::metrics::ViewRegistry>(),
          std::move(resource));
  reader_.reset(new grpc::testing::MockMetricReader);
  meter_provider->AddMetricReader(reader_);
  grpc_core::CoreConfiguration::Reset();
  grpc::internal::OpenTelemetryPluginBuilder ot_builder;
  ot_builder.EnableMetrics(metric_names);
  if (!test_no_meter_provider) {
    auto meter_provider =
        std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
    reader_.reset(new grpc::testing::MockMetricReader);
    meter_provider->AddMetricReader(reader_);
    ot_builder.SetMeterProvider(std::move(meter_provider));
  }
  ot_builder.SetLabelsInjector(std::move(labels_injector));
  ot_builder.SetTargetSelector(std::move(target_selector));
  ot_builder.SetTargetAttributeFilter(std::move(target_attribute_filter));
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
  canonical_server_address_ = absl::StrCat("dns:///", server_address_);

  stub_ = EchoTestService::NewStub(
      grpc::CreateChannel(server_address_, grpc::InsecureChannelCredentials()));
}

void OTelPluginEnd2EndTest::TearDown() {
  server_->Shutdown();
  grpc_shutdown_blocking();
  delete grpc_core::ServerCallTracerFactory::Get(grpc_core::ChannelArgs());
  grpc_core::ServerCallTracerFactory::RegisterGlobal(nullptr);
}

void OTelPluginEnd2EndTest::ResetStub(std::shared_ptr<Channel> channel) {
  stub_ = EchoTestService::NewStub(std::move(channel));
}

void OTelPluginEnd2EndTest::SendRPC() {
  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub_->Echo(&context, request, &response);
}

absl::flat_hash_map<
    std::string, std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
OTelPluginEnd2EndTest::ReadCurrentMetricsData(
    absl::AnyInvocable<
        bool(const absl::flat_hash_map<
             std::string,
             std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&)>
        continue_predicate) {
  absl::flat_hash_map<
      std::string,
      std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
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
            data[md.instrument_descriptor.name_].push_back(dp);
          }
        }
      }
      return true;
    });
  } while (continue_predicate(data) && deadline > absl::Now());
  return data;
}

}  // namespace testing
}  // namespace grpc
