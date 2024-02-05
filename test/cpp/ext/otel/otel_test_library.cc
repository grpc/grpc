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
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/notification.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"

namespace grpc {
namespace testing {

#define GRPC_ARG_LABELS_TO_INJECT "grpc.testing.labels_to_inject"

// A subchannel filter that adds the service labels for test to the
// CallAttemptTracer in a call.
class AddServiceLabelsFilter : public grpc_core::ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<AddServiceLabelsFilter> Create(
      const grpc_core::ChannelArgs& args, ChannelFilter::Args /*filter_args*/) {
    return AddServiceLabelsFilter(
        args.GetPointer<const std::map<std::string, std::string>>(
            GRPC_ARG_LABELS_TO_INJECT));
  }

  grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle> MakeCallPromise(
      grpc_core::CallArgs call_args,
      grpc_core::NextPromiseFactory next_promise_factory) override {
    using CallAttemptTracer = grpc_core::ClientCallTracer::CallAttemptTracer;
    auto* call_context = grpc_core::GetContext<grpc_call_context_element>();
    auto* call_tracer = static_cast<CallAttemptTracer*>(
        call_context[GRPC_CONTEXT_CALL_TRACER].value);
    EXPECT_NE(call_tracer, nullptr);
    call_tracer->AddOptionalLabels(
        CallAttemptTracer::OptionalLabelComponent::kXdsServiceLabels,
        std::make_shared<std::map<std::string, std::string>>(
            *labels_to_inject_));
    return next_promise_factory(std::move(call_args));
  }

 private:
  explicit AddServiceLabelsFilter(
      const std::map<std::string, std::string>* labels_to_inject)
      : labels_to_inject_(labels_to_inject) {}

  const std::map<std::string, std::string>* labels_to_inject_;
};

const grpc_channel_filter AddServiceLabelsFilter::kFilter =
    grpc_core::MakePromiseBasedFilter<AddServiceLabelsFilter,
                                      grpc_core::FilterEndpoint::kClient>(
        "add_service_labels_filter");

void OpenTelemetryPluginEnd2EndTest::Init(Options config) {
  // We are resetting the MeterProvider and OpenTelemetry plugin at the start
  // of each test to avoid test results from one test carrying over to another
  // test. (Some measurements can get arbitrarily delayed.)
  auto meter_provider =
      std::make_shared<opentelemetry::sdk::metrics::MeterProvider>(
          std::make_unique<opentelemetry::sdk::metrics::ViewRegistry>(),
          *config.resource);
  reader_.reset(new grpc::testing::MockMetricReader);
  meter_provider->AddMetricReader(reader_);
  grpc_core::CoreConfiguration::Reset();
  grpc::internal::OpenTelemetryPluginBuilderImpl ot_builder;
  ot_builder.DisableAllMetrics();
  for (const auto& metric_name : config.metric_names) {
    ot_builder.EnableMetric(metric_name);
  }
  if (config.use_meter_provider) {
    auto meter_provider =
        std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
    reader_.reset(new grpc::testing::MockMetricReader);
    meter_provider->AddMetricReader(reader_);
    ot_builder.SetMeterProvider(std::move(meter_provider));
  }
  ot_builder.SetTargetSelector(std::move(config.target_selector));
  ot_builder.SetServerSelector(std::move(config.server_selector));
  ot_builder.SetTargetAttributeFilter(
      std::move(config.target_attribute_filter));
  ot_builder.SetGenericMethodAttributeFilter(
      std::move(config.generic_method_attribute_filter));
  for (auto& option : config.plugin_options) {
    ot_builder.AddPluginOption(std::move(option));
  }
  ASSERT_EQ(ot_builder.BuildAndRegisterGlobal(), absl::OkStatus());
  ChannelArguments channel_args;
  if (!config.labels_to_inject.empty()) {
    labels_to_inject_ = config.labels_to_inject;
    grpc_core::CoreConfiguration::RegisterBuilder(
        [](grpc_core::CoreConfiguration::Builder* builder) mutable {
          builder->channel_init()->RegisterFilter(
              GRPC_CLIENT_SUBCHANNEL, &AddServiceLabelsFilter::kFilter);
        });
    channel_args.SetPointer(GRPC_ARG_LABELS_TO_INJECT, &labels_to_inject_);
  }
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

  auto channel = grpc::CreateCustomChannel(
      server_address_, grpc::InsecureChannelCredentials(), channel_args);
  stub_ = EchoTestService::NewStub(channel);
  generic_stub_ = std::make_unique<GenericStub>(std::move(channel));
}

void OpenTelemetryPluginEnd2EndTest::TearDown() {
  server_->Shutdown();
  grpc_shutdown_blocking();
  grpc_core::ServerCallTracerFactory::TestOnlyReset();
}

void OpenTelemetryPluginEnd2EndTest::ResetStub(
    std::shared_ptr<Channel> channel) {
  stub_ = EchoTestService::NewStub(channel);
  generic_stub_ = std::make_unique<GenericStub>(std::move(channel));
}

void OpenTelemetryPluginEnd2EndTest::SendRPC() {
  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub_->Echo(&context, request, &response);
}

void OpenTelemetryPluginEnd2EndTest::SendGenericRPC() {
  grpc::ClientContext context;
  EchoRequest request;
  std::unique_ptr<ByteBuffer> send_buf = SerializeToByteBuffer(&request);
  ByteBuffer recv_buf;
  grpc_core::Notification notify;
  generic_stub_->UnaryCall(&context, absl::StrCat("/", kGenericMethodName),
                           StubOptions(), send_buf.get(), &recv_buf,
                           [&](grpc::Status /*s*/) { notify.Notify(); });
  notify.WaitForNotificationWithTimeout(absl::Seconds(5));
}

absl::flat_hash_map<
    std::string, std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
OpenTelemetryPluginEnd2EndTest::ReadCurrentMetricsData(
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
