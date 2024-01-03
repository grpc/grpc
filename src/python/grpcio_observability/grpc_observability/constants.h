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

#ifndef GRPC_PYTHON_OBSERVABILITY_CONSTANTS_H
#define GRPC_PYTHON_OBSERVABILITY_CONSTANTS_H

#include <string>

namespace grpc_observability {

const std::string kClientMethod = "grpc_client_method";
const std::string kClientStatus = "grpc_client_status";
const std::string kServerMethod = "grpc_server_method";
const std::string kServerStatus = "grpc_server_status";

typedef enum { kMeasurementDouble = 0, kMeasurementInt } MeasurementType;

typedef enum { kSpanData = 0, kMetricData } DataType;

typedef enum {
  kRpcClientApiLatencyMeasureName = 0,
  kRpcClientSentMessagesPerRpcMeasureName,
  kRpcClientSentBytesPerRpcMeasureName,
  kRpcClientReceivedMessagesPerRpcMeasureName,
  kRpcClientReceivedBytesPerRpcMeasureName,
  kRpcClientRoundtripLatencyMeasureName,
  kRpcClientCompletedRpcMeasureName,
  kRpcClientServerLatencyMeasureName,
  kRpcClientStartedRpcsMeasureName,
  kRpcClientRetriesPerCallMeasureName,
  kRpcClientTransparentRetriesPerCallMeasureName,
  kRpcClientRetryDelayPerCallMeasureName,
  kRpcClientTransportLatencyMeasureName,
  kRpcServerSentMessagesPerRpcMeasureName,
  kRpcServerSentBytesPerRpcMeasureName,
  kRpcServerReceivedMessagesPerRpcMeasureName,
  kRpcServerReceivedBytesPerRpcMeasureName,
  kRpcServerServerLatencyMeasureName,
  kRpcServerCompletedRpcMeasureName,
  kRpcServerStartedRpcsMeasureName
} MetricsName;

}  // namespace grpc_observability

#endif  // GRPC_PYTHON_OBSERVABILITY_CONSTANTS_H
