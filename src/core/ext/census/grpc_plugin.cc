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

#include "src/core/ext/census/channel_filter.h"
#include "src/core/ext/census/client_filter.h"
#include "src/core/ext/census/grpc_plugin.h"
#include "src/core/ext/census/measures.h"
#include "src/core/ext/census/server_filter.h"
#include "src/core/lib/surface/channel_init.h"

namespace opencensus {

void RegisterGrpcPlugin() {
  grpc::RegisterChannelFilter<opencensus::CensusChannelData,
                              opencensus::CensusClientCallData>(
      "opencensus_client", GRPC_CLIENT_CHANNEL, INT_MAX /* priority */,
      nullptr /* condition function */);
  grpc::RegisterChannelFilter<opencensus::CensusChannelData,
                              opencensus::CensusServerCallData>(
      "opencensus_server", GRPC_SERVER_CHANNEL, INT_MAX /* priority */,
      nullptr /* condition function */);

  // Access measures to ensure they are initialized. Otherwise, creating a view
  // before the first RPC would cause an error.
  RpcClientErrorCount();
  RpcClientRequestBytes();
  RpcClientResponseBytes();
  RpcClientRoundtripLatency();
  RpcClientServerElapsedTime();
  RpcClientStartedCount();
  RpcClientFinishedCount();
  RpcClientRequestCount();
  RpcClientResponseCount();
  RpcServerErrorCount();
  RpcServerRequestBytes();
  RpcServerResponseBytes();
  RpcServerServerElapsedTime();
  RpcServerStartedCount();
  RpcServerFinishedCount();
  RpcServerRequestCount();
  RpcServerResponseCount();
}

// These measure definitions should be kept in sync across opencensus
// implementations--see
// https://github.com/census-instrumentation/opencensus-java/blob/master/contrib/grpc_metrics/src/main/java/io/opencensus/contrib/grpc/metrics/RpcMeasureConstants.java.
ABSL_CONST_INIT const absl::string_view kMethodTagKey = "method";
ABSL_CONST_INIT const absl::string_view kStatusTagKey = "status";

// Client
ABSL_CONST_INIT const absl::string_view kRpcClientErrorCountMeasureName =
    "grpc.io/client/error_count";

ABSL_CONST_INIT const absl::string_view kRpcClientRequestBytesMeasureName =
    "grpc.io/client/request_bytes";

ABSL_CONST_INIT const absl::string_view kRpcClientResponseBytesMeasureName =
    "grpc.io/client/response_bytes";

ABSL_CONST_INIT const absl::string_view kRpcClientRoundtripLatencyMeasureName =
    "grpc.io/client/roundtrip_latency";

ABSL_CONST_INIT const absl::string_view kRpcClientServerElapsedTimeMeasureName =
    "grpc.io/client/server_elapsed_time";

ABSL_CONST_INIT const absl::string_view kRpcClientStartedCountMeasureName =
    "grpc.io/client/started_count";

ABSL_CONST_INIT const absl::string_view kRpcClientFinishedCountMeasureName =
    "grpc.io/client/finished_count";

ABSL_CONST_INIT const absl::string_view kRpcClientRequestCountMeasureName =
    "grpc.io/client/request_count";

ABSL_CONST_INIT const absl::string_view kRpcClientResponseCountMeasureName =
    "grpc.io/client/response_count";

// Server
ABSL_CONST_INIT const absl::string_view kRpcServerErrorCountMeasureName =
    "grpc.io/server/error_count";

ABSL_CONST_INIT const absl::string_view kRpcServerRequestBytesMeasureName =
    "grpc.io/server/request_bytes";

ABSL_CONST_INIT const absl::string_view kRpcServerResponseBytesMeasureName =
    "grpc.io/server/response_bytes";

ABSL_CONST_INIT const absl::string_view kRpcServerServerElapsedTimeMeasureName =
    "grpc.io/server/server_elapsed_time";

ABSL_CONST_INIT const absl::string_view kRpcServerStartedCountMeasureName =
    "grpc.io/server/started_count";

ABSL_CONST_INIT const absl::string_view kRpcServerFinishedCountMeasureName =
    "grpc.io/server/finished_count";

ABSL_CONST_INIT const absl::string_view kRpcServerRequestCountMeasureName =
    "grpc.io/server/request_count";

ABSL_CONST_INIT const absl::string_view kRpcServerResponseCountMeasureName =
    "grpc.io/server/response_count";

}  // namespace opencensus
