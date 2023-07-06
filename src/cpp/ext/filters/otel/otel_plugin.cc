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

#include <grpc/support/port_platform.h>

#include "src/cpp/ext/filters/otel/otel_plugin.h"

#include <limits.h>

#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/unique_ptr.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/cpp/ext/filters/otel/otel_client_filter.h"
#include "src/cpp/ext/filters/otel/otel_server_call_tracer.h"

namespace grpc {
namespace internal {

// TODO(yashykt): Extend this to allow multiple OTel plugins to be registered in
// the same binary.
struct OTelPluginState* g_otel_plugin_state_;

const struct OTelPluginState& OTelPluginState() {
  GPR_DEBUG_ASSERT(g_otel_plugin_state_ != nullptr);
  return *g_otel_plugin_state_;
}

void RegisterOpenTelemetryPlugin() {
  auto meter_provider = opentelemetry::metrics::Provider::GetMeterProvider();
  auto meter = meter_provider->GetMeter("grpc");
  g_otel_plugin_state_ = new struct OTelPluginState;
  g_otel_plugin_state_->client.attempt.started =
      meter->CreateUInt64Counter("grpc.client.attempt.started");
  g_otel_plugin_state_->client.attempt.duration =
      meter->CreateDoubleHistogram("grpc.client.attempt.duration");
  g_otel_plugin_state_->client.attempt.sent_total_compressed_message_size =
      meter->CreateUInt64Histogram(
          "grpc.client.attempt.sent_total_compressed_message_size");
  g_otel_plugin_state_->client.attempt.rcvd_total_compressed_message_size =
      meter->CreateUInt64Histogram(
          "grpc.client.attempt.rcvd_total_compressed_message_size");
  g_otel_plugin_state_->server.call.started =
      meter->CreateUInt64Counter("grpc.server.call.started");
  g_otel_plugin_state_->server.call.duration =
      meter->CreateDoubleHistogram("grpc.server.call.duration");
  g_otel_plugin_state_->server.call.sent_total_compressed_message_size =
      meter->CreateUInt64Histogram(
          "grpc.server.call.sent_total_compressed_message_size");
  g_otel_plugin_state_->server.call.rcvd_total_compressed_message_size =
      meter->CreateUInt64Histogram(
          "grpc.server.call.rcvd_total_compressed_message_size");
  grpc_core::ServerCallTracerFactory::RegisterGlobal(
      new grpc::internal::OpenTelemetryServerCallTracerFactory);
  grpc_core::CoreConfiguration::RegisterBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->channel_init()->RegisterStage(
            GRPC_CLIENT_CHANNEL, /*priority=*/INT_MAX,
            [](grpc_core::ChannelStackBuilder* builder) {
              builder->PrependFilter(
                  &grpc::internal::OpenTelemetryClientFilter::kFilter);
              return true;
            });
      });
}

absl::string_view OTelMethodKey() { return "grpc.method"; }

absl::string_view OTelStatusKey() { return "grpc.status"; }

}  // namespace internal
}  // namespace grpc
