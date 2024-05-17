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

#include <atomic>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"

#include <grpcpp/grpcpp.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/notification.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"

namespace grpc {
namespace testing {

#define GRPC_ARG_LABELS_TO_INJECT "grpc.testing.labels_to_inject"

// A subchannel filter that adds the service labels for test to the
// CallAttemptTracer in a call.
class AddLabelsFilter : public grpc_core::ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  explicit AddLabelsFilter(
      std::map<grpc_core::ClientCallTracer::CallAttemptTracer::OptionalLabelKey,
               grpc_core::RefCountedStringValue>
          labels_to_inject)
      : labels_to_inject_(std::move(labels_to_inject)) {}

  static absl::StatusOr<std::unique_ptr<AddLabelsFilter>> Create(
      const grpc_core::ChannelArgs& args, ChannelFilter::Args /*filter_args*/) {
    return absl::make_unique<AddLabelsFilter>(
        *args.GetPointer<std::map<
             grpc_core::ClientCallTracer::CallAttemptTracer::OptionalLabelKey,
             grpc_core::RefCountedStringValue>>(GRPC_ARG_LABELS_TO_INJECT));
  }

  grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle> MakeCallPromise(
      grpc_core::CallArgs call_args,
      grpc_core::NextPromiseFactory next_promise_factory) override {
    using CallAttemptTracer = grpc_core::ClientCallTracer::CallAttemptTracer;
    auto* call_context = grpc_core::GetContext<grpc_call_context_element>();
    auto* call_tracer = static_cast<CallAttemptTracer*>(
        call_context[GRPC_CONTEXT_CALL_TRACER].value);
    EXPECT_NE(call_tracer, nullptr);
    for (const auto& pair : labels_to_inject_) {
      call_tracer->SetOptionalLabel(pair.first, pair.second);
    }
    return next_promise_factory(std::move(call_args));
  }

 private:
  const std::map<
      grpc_core::ClientCallTracer::CallAttemptTracer::OptionalLabelKey,
      grpc_core::RefCountedStringValue>
      labels_to_inject_;
};

const grpc_channel_filter AddLabelsFilter::kFilter =
    grpc_core::MakePromiseBasedFilter<AddLabelsFilter,
                                      grpc_core::FilterEndpoint::kClient>(
        "add_service_labels_filter");

OpenTelemetryPluginEnd2EndTest::MetricsCollectorThread::MetricsCollectorThread(
    OpenTelemetryPluginEnd2EndTest* test, grpc_core::Duration interval,
    int iterations,
    std::function<
        bool(const absl::flat_hash_map<
             std::string,
             std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&)>
        predicate)
    : test_(test),
      interval_(interval),
      iterations_(iterations),
      predicate_(std::move(predicate)),
      thread_(&MetricsCollectorThread::Run, this) {}

OpenTelemetryPluginEnd2EndTest::MetricsCollectorThread::
    ~MetricsCollectorThread() {
  if (!finished_) {
    thread_.join();
  }
}

void OpenTelemetryPluginEnd2EndTest::MetricsCollectorThread::Run() {
  int i = 0;
  while (i++ < iterations_ || (iterations_ == -1 && !finished_)) {
    auto data_points = test_->ReadCurrentMetricsData(predicate_);
    for (auto data : data_points) {
      auto iter = data_points_.find(data.first);
      if (iter == data_points_.end()) {
        data_points_[data.first] = std::move(data.second);
      } else {
        for (auto point : data.second) {
          iter->second.push_back(std::move(point));
        }
      }
    }
    absl::SleepFor(absl::Milliseconds(interval_.millis()));
  }
}

const OpenTelemetryPluginEnd2EndTest::MetricsCollectorThread::ResultType&
OpenTelemetryPluginEnd2EndTest::MetricsCollectorThread::Stop() {
  finished_ = true;
  thread_.join();
  return data_points_;
}

void OpenTelemetryPluginEnd2EndTest::Init(Options config) {
  grpc_core::CoreConfiguration::Reset();
  ChannelArguments channel_args;
  if (!config.labels_to_inject.empty()) {
    labels_to_inject_ = std::move(config.labels_to_inject);
    grpc_core::CoreConfiguration::RegisterBuilder(
        [](grpc_core::CoreConfiguration::Builder* builder) mutable {
          builder->channel_init()->RegisterFilter(GRPC_CLIENT_SUBCHANNEL,
                                                  &AddLabelsFilter::kFilter);
        });
    channel_args.SetPointer(GRPC_ARG_LABELS_TO_INJECT, &labels_to_inject_);
  }
  if (!config.service_config.empty()) {
    channel_args.SetString(GRPC_ARG_SERVICE_CONFIG, config.service_config);
  }
  reader_ = BuildAndRegisterOpenTelemetryPlugin(std::move(config));
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
  grpc_core::GlobalStatsPluginRegistryTestPeer::
      ResetGlobalStatsPluginRegistry();
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
        continue_predicate,
    opentelemetry::sdk::metrics::MetricReader* reader) {
  if (reader == nullptr) {
    reader = reader_.get();
  }
  absl::flat_hash_map<
      std::string,
      std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
      data;
  auto deadline = absl::Now() + absl::Seconds(5);
  do {
    reader->Collect([&](opentelemetry::sdk::metrics::ResourceMetrics& rm) {
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

std::shared_ptr<opentelemetry::sdk::metrics::MetricReader>
OpenTelemetryPluginEnd2EndTest::BuildAndRegisterOpenTelemetryPlugin(
    OpenTelemetryPluginEnd2EndTest::Options options) {
  grpc::internal::OpenTelemetryPluginBuilderImpl ot_builder;
  // We are resetting the MeterProvider and OpenTelemetry plugin at the start
  // of each test to avoid test results from one test carrying over to another
  // test. (Some measurements can get arbitrarily delayed.)
  auto meter_provider =
      std::make_shared<opentelemetry::sdk::metrics::MeterProvider>(
          std::make_unique<opentelemetry::sdk::metrics::ViewRegistry>(),
          *options.resource);
  std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader =
      std::make_shared<grpc::testing::MockMetricReader>();
  meter_provider->AddMetricReader(reader);
  ot_builder.DisableAllMetrics();
  ot_builder.EnableMetrics(options.metric_names);
  if (options.use_meter_provider) {
    auto meter_provider =
        std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
    reader.reset(new grpc::testing::MockMetricReader);
    meter_provider->AddMetricReader(reader);
    ot_builder.SetMeterProvider(std::move(meter_provider));
  }
  ot_builder.SetChannelScopeFilter(std::move(options.channel_scope_filter));
  ot_builder.SetServerSelector(std::move(options.server_selector));
  ot_builder.SetTargetAttributeFilter(
      std::move(options.target_attribute_filter));
  ot_builder.SetGenericMethodAttributeFilter(
      std::move(options.generic_method_attribute_filter));
  for (auto& option : options.plugin_options) {
    ot_builder.AddPluginOption(std::move(option));
  }
  for (auto& optional_label_key : options.optional_label_keys) {
    ot_builder.AddOptionalLabel(optional_label_key);
  }
  EXPECT_EQ(ot_builder.BuildAndRegisterGlobal(), absl::OkStatus());
  return reader;
}

}  // namespace testing
}  // namespace grpc
