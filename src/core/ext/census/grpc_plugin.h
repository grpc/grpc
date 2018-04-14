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

#ifndef GRPC_CORE_EXT_CENSUS_GRPC_PLUGIN_H
#define GRPC_CORE_EXT_CENSUS_GRPC_PLUGIN_H

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"

namespace opencensus {

// Registers the OpenCensus plugin with gRPC, so that it will be used for future
// RPCs. This must be called before any views are created on the measures
// defined below.
void RegisterGrpcPlugin();

// The tag key for the RPC method and status, set for all values recorded for
// the following measures.
extern const absl::string_view kMethodTagKey;
extern const absl::string_view kStatusTagKey;

// Names of measures used by the plugin--users can create views on these
// measures but should not record data for them.
extern const absl::string_view kRpcClientErrorCountMeasureName;
extern const absl::string_view kRpcClientRequestBytesMeasureName;
extern const absl::string_view kRpcClientResponseBytesMeasureName;
extern const absl::string_view kRpcClientRoundtripLatencyMeasureName;
extern const absl::string_view kRpcClientServerElapsedTimeMeasureName;
extern const absl::string_view kRpcClientStartedCountMeasureName;
extern const absl::string_view kRpcClientFinishedCountMeasureName;
extern const absl::string_view kRpcClientRequestCountMeasureName;
extern const absl::string_view kRpcClientResponseCountMeasureName;

extern const absl::string_view kRpcServerErrorCountMeasureName;
extern const absl::string_view kRpcServerRequestBytesMeasureName;
extern const absl::string_view kRpcServerResponseBytesMeasureName;
extern const absl::string_view kRpcServerServerElapsedTimeMeasureName;
extern const absl::string_view kRpcServerStartedCountMeasureName;
extern const absl::string_view kRpcServerFinishedCountMeasureName;
extern const absl::string_view kRpcServerRequestCountMeasureName;
extern const absl::string_view kRpcServerResponseCountMeasureName;

}  // namespace opencensus

#endif /* GRPC_CORE_EXT_CENSUS_GRPC_PLUGIN_H */
