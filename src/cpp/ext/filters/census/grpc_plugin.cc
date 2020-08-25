/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/cpp/ext/filters/census/grpc_plugin.h"

#include <grpcpp/server_context.h>

#include "opencensus/tags/tag_key.h"
#include "opencensus/trace/span.h"
#include "src/cpp/ext/filters/census/channel_filter.h"
#include "src/cpp/ext/filters/census/client_filter.h"
#include "src/cpp/ext/filters/census/measures.h"
#include "src/cpp/ext/filters/census/server_filter.h"

namespace grpc {

void RegisterOpenCensusPlugin() {
  RegisterChannelFilter<CensusChannelData, CensusClientCallData>(
      "opencensus_client", GRPC_CLIENT_CHANNEL, INT_MAX /* priority */,
      nullptr /* condition function */);
  RegisterChannelFilter<CensusChannelData, CensusServerCallData>(
      "opencensus_server", GRPC_SERVER_CHANNEL, INT_MAX /* priority */,
      nullptr /* condition function */);

  // Access measures to ensure they are initialized. Otherwise, creating a view
  // before the first RPC would cause an error.
  RpcClientSentBytesPerRpc();
  RpcClientReceivedBytesPerRpc();
  RpcClientRoundtripLatency();
  RpcClientServerLatency();
  RpcClientSentMessagesPerRpc();
  RpcClientReceivedMessagesPerRpc();

  RpcServerSentBytesPerRpc();
  RpcServerReceivedBytesPerRpc();
  RpcServerServerLatency();
  RpcServerSentMessagesPerRpc();
  RpcServerReceivedMessagesPerRpc();
}

::opencensus::trace::Span GetSpanFromServerContext(
    grpc::ServerContext* context) {
  if (context == nullptr) return opencensus::trace::Span::BlankSpan();

  return reinterpret_cast<const grpc::CensusContext*>(context->census_context())
      ->Span();
}

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
}  // namespace grpc
