//
//
// Copyright 2018 gRPC authors.
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

#include "src/cpp/ext/filters/census/grpc_plugin.h"

#include <grpc/support/port_platform.h>
#include <grpcpp/opencensus.h>
#include <grpcpp/server_context.h>

#include <atomic>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "opencensus/tags/tag_key.h"
#include "opencensus/trace/span.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/filters/logging/logging_filter.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/cpp/ext/filters/census/client_filter.h"
#include "src/cpp/ext/filters/census/measures.h"
#include "src/cpp/ext/filters/census/server_call_tracer.h"

namespace grpc {

void RegisterOpenCensusPlugin() {
  grpc_core::ServerCallTracerFactory::RegisterGlobal(
      new grpc::internal::OpenCensusServerCallTracerFactory);
  grpc_core::CoreConfiguration::RegisterBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->channel_init()
            ->RegisterFilter(GRPC_CLIENT_CHANNEL,
                             &grpc::internal::OpenCensusClientFilter::kFilter)
            .Before<grpc_core::ClientLoggingFilter>();
      });

  // Access measures to ensure they are initialized. Otherwise, creating a view
  // before the first RPC would cause an error.
  RpcClientSentBytesPerRpc();
  RpcClientReceivedBytesPerRpc();
  RpcClientRoundtripLatency();
  RpcClientServerLatency();
  RpcClientStartedRpcs();
  RpcClientSentMessagesPerRpc();
  RpcClientReceivedMessagesPerRpc();
  RpcClientRetriesPerCall();
  RpcClientTransparentRetriesPerCall();
  RpcClientRetryDelayPerCall();
  RpcClientTransportLatency();
  internal::RpcClientApiLatency();

  RpcServerSentBytesPerRpc();
  RpcServerReceivedBytesPerRpc();
  RpcServerServerLatency();
  RpcServerStartedRpcs();
  RpcServerSentMessagesPerRpc();
  RpcServerReceivedMessagesPerRpc();
}

::opencensus::trace::Span GetSpanFromServerContext(
    grpc::ServerContext* context) {
  if (context == nullptr) return opencensus::trace::Span::BlankSpan();

  return reinterpret_cast<const grpc::experimental::CensusContext*>(
             context->census_context())
      ->Span();
}

namespace experimental {

// These measure definitions should be kept in sync across opencensus
// implementations--see
// https://github.com/census-instrumentation/opencensus-java/blob/master/contrib/grpc_metrics/src/main/java/io/opencensus/contrib/grpc/metrics/RpcMeasureConstants.java.
::opencensus::tags::TagKey ClientMethodTagKey() {
  static const auto method_tag_key =
      ::opencensus::tags::TagKey::Register("grpc_client_method");
  return method_tag_key;
}

::opencensus::tags::TagKey ClientStatusTagKey() {
  static const auto status_tag_key =
      ::opencensus::tags::TagKey::Register("grpc_client_status");
  return status_tag_key;
}

::opencensus::tags::TagKey ServerMethodTagKey() {
  static const auto method_tag_key =
      ::opencensus::tags::TagKey::Register("grpc_server_method");
  return method_tag_key;
}

::opencensus::tags::TagKey ServerStatusTagKey() {
  static const auto status_tag_key =
      ::opencensus::tags::TagKey::Register("grpc_server_status");
  return status_tag_key;
}

// Client
ABSL_CONST_INIT const absl::string_view
    kRpcClientSentMessagesPerRpcMeasureName =
        "grpc.io/client/sent_messages_per_rpc";

ABSL_CONST_INIT const absl::string_view kRpcClientSentBytesPerRpcMeasureName =
    "grpc.io/client/sent_bytes_per_rpc";

ABSL_CONST_INIT const absl::string_view
    kRpcClientReceivedMessagesPerRpcMeasureName =
        "grpc.io/client/received_messages_per_rpc";

ABSL_CONST_INIT const absl::string_view
    kRpcClientReceivedBytesPerRpcMeasureName =
        "grpc.io/client/received_bytes_per_rpc";

ABSL_CONST_INIT const absl::string_view kRpcClientRoundtripLatencyMeasureName =
    "grpc.io/client/roundtrip_latency";

ABSL_CONST_INIT const absl::string_view kRpcClientServerLatencyMeasureName =
    "grpc.io/client/server_latency";

ABSL_CONST_INIT const absl::string_view kRpcClientStartedRpcsMeasureName =
    "grpc.io/client/started_rpcs";

ABSL_CONST_INIT const absl::string_view kRpcClientRetriesPerCallMeasureName =
    "grpc.io/client/retries_per_call";

ABSL_CONST_INIT const absl::string_view
    kRpcClientTransparentRetriesPerCallMeasureName =
        "grpc.io/client/transparent_retries_per_call";

ABSL_CONST_INIT const absl::string_view kRpcClientRetryDelayPerCallMeasureName =
    "grpc.io/client/retry_delay_per_call";

ABSL_CONST_INIT const absl::string_view kRpcClientTransportLatencyMeasureName =
    "grpc.io/client/transport_latency";

// Server
ABSL_CONST_INIT const absl::string_view
    kRpcServerSentMessagesPerRpcMeasureName =
        "grpc.io/server/sent_messages_per_rpc";

ABSL_CONST_INIT const absl::string_view kRpcServerSentBytesPerRpcMeasureName =
    "grpc.io/server/sent_bytes_per_rpc";

ABSL_CONST_INIT const absl::string_view
    kRpcServerReceivedMessagesPerRpcMeasureName =
        "grpc.io/server/received_messages_per_rpc";

ABSL_CONST_INIT const absl::string_view
    kRpcServerReceivedBytesPerRpcMeasureName =
        "grpc.io/server/received_bytes_per_rpc";

ABSL_CONST_INIT const absl::string_view kRpcServerServerLatencyMeasureName =
    "grpc.io/server/server_latency";

ABSL_CONST_INIT const absl::string_view kRpcServerStartedRpcsMeasureName =
    "grpc.io/server/started_rpcs";

}  // namespace experimental

namespace internal {

ABSL_CONST_INIT const absl::string_view kRpcClientApiLatencyMeasureName =
    "grpc.io/client/api_latency";
namespace {
std::atomic<bool> g_open_census_stats_enabled(true);
std::atomic<bool> g_open_census_tracing_enabled(true);
}  // namespace

//
// OpenCensusRegistry
//

OpenCensusRegistry& OpenCensusRegistry::Get() {
  static OpenCensusRegistry* registry = new OpenCensusRegistry;
  return *registry;
}

::opencensus::tags::TagMap OpenCensusRegistry::PopulateTagMapWithConstantLabels(
    const ::opencensus::tags::TagMap& tag_map) {
  std::vector<std::pair<::opencensus::tags::TagKey, std::string>> tags =
      tag_map.tags();
  for (const auto& label : ConstantLabels()) {
    tags.emplace_back(label.tag_key, label.value);
  }
  return ::opencensus::tags::TagMap(std::move(tags));
}

void OpenCensusRegistry::PopulateCensusContextWithConstantAttributes(
    grpc::experimental::CensusContext* context) {
  for (const auto& attribute : ConstantAttributes()) {
    context->AddSpanAttribute(attribute.key, attribute.value);
  }
}

void EnableOpenCensusStats(bool enable) {
  g_open_census_stats_enabled = enable;
}

void EnableOpenCensusTracing(bool enable) {
  g_open_census_tracing_enabled = enable;
}

bool OpenCensusStatsEnabled() {
  return g_open_census_stats_enabled.load(std::memory_order_relaxed);
}

bool OpenCensusTracingEnabled() {
  return g_open_census_tracing_enabled.load(std::memory_order_relaxed);
}

}  // namespace internal

}  // namespace grpc
