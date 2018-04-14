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

#include "src/core/ext/census/measures.h"

#include "opencensus/stats/stats.h"
#include "src/core/ext/census/grpc_plugin.h"

namespace opencensus {

// These measure definitions should be kept in sync across opencensus
// implementations--see
// https://github.com/census-instrumentation/opencensus-java/blob/master/contrib/grpc_metrics/src/main/java/io/opencensus/contrib/grpc/metrics/RpcMeasureConstants.java.

namespace {

// Unit constants
constexpr char kUnitBytes[] = "By";
constexpr char kUnitMilliseconds[] = "ms";
constexpr char kCount[] = "1";

}  // namespace

// Client
stats::MeasureInt RpcClientErrorCount() {
  static stats::MeasureInt measure = stats::MeasureRegistry::RegisterInt(
      kRpcClientErrorCountMeasureName, kCount, "RPC Errors");
  return measure;
}

stats::MeasureDouble RpcClientRequestBytes() {
  static stats::MeasureDouble measure = stats::MeasureRegistry::RegisterDouble(
      kRpcClientRequestBytesMeasureName, kUnitBytes, "Request bytes");
  return measure;
}

stats::MeasureDouble RpcClientResponseBytes() {
  static stats::MeasureDouble measure = stats::MeasureRegistry::RegisterDouble(
      kRpcClientResponseBytesMeasureName, kUnitBytes, "Response bytes");
  return measure;
}

stats::MeasureDouble RpcClientRoundtripLatency() {
  static stats::MeasureDouble measure = stats::MeasureRegistry::RegisterDouble(
      kRpcClientRoundtripLatencyMeasureName, kUnitMilliseconds,
      "RPC roundtrip latency msec");
  return measure;
}

stats::MeasureDouble RpcClientServerElapsedTime() {
  static stats::MeasureDouble measure = stats::MeasureRegistry::RegisterDouble(
      kRpcClientServerElapsedTimeMeasureName, kUnitMilliseconds,
      "Server elapsed time in msecs");
  return measure;
}

stats::MeasureInt RpcClientStartedCount() {
  static stats::MeasureInt measure = stats::MeasureRegistry::RegisterInt(
      kRpcClientStartedCountMeasureName, kCount,
      "Number of client RPCs (streams) started");
  return measure;
}

stats::MeasureInt RpcClientFinishedCount() {
  static stats::MeasureInt measure = stats::MeasureRegistry::RegisterInt(
      kRpcClientFinishedCountMeasureName, kCount,
      "Number of client RPCs (streams) finished");
  return measure;
}

stats::MeasureInt RpcClientRequestCount() {
  static stats::MeasureInt measure = stats::MeasureRegistry::RegisterInt(
      kRpcClientRequestCountMeasureName, kCount,
      "Number of client RPC request messages");
  return measure;
}

stats::MeasureInt RpcClientResponseCount() {
  static stats::MeasureInt measure = stats::MeasureRegistry::RegisterInt(
      kRpcClientResponseCountMeasureName, kCount,
      "Number of client RPC response messages");
  return measure;
}

// Server
stats::MeasureInt RpcServerErrorCount() {
  static stats::MeasureInt measure = stats::MeasureRegistry::RegisterInt(
      kRpcServerErrorCountMeasureName, kCount, "RPC Errors");
  return measure;
}

stats::MeasureDouble RpcServerRequestBytes() {
  static stats::MeasureDouble measure = stats::MeasureRegistry::RegisterDouble(
      kRpcServerRequestBytesMeasureName, kUnitBytes, "Request bytes");
  return measure;
}

stats::MeasureDouble RpcServerResponseBytes() {
  static stats::MeasureDouble measure = stats::MeasureRegistry::RegisterDouble(
      kRpcServerResponseBytesMeasureName, kUnitBytes, "Response bytes");
  return measure;
}

stats::MeasureDouble RpcServerServerElapsedTime() {
  static stats::MeasureDouble measure = stats::MeasureRegistry::RegisterDouble(
      kRpcServerServerElapsedTimeMeasureName, kUnitMilliseconds,
      "Server elapsed time in msecs");
  return measure;
}

stats::MeasureInt RpcServerStartedCount() {
  static stats::MeasureInt measure = stats::MeasureRegistry::RegisterInt(
      kRpcServerStartedCountMeasureName, kCount,
      "Number of server RPCs (streams) started");
  return measure;
}

stats::MeasureInt RpcServerFinishedCount() {
  static stats::MeasureInt measure = stats::MeasureRegistry::RegisterInt(
      kRpcServerFinishedCountMeasureName, kCount,
      "Number of server RPCs (streams) finished");
  return measure;
}

stats::MeasureInt RpcServerRequestCount() {
  static stats::MeasureInt measure = stats::MeasureRegistry::RegisterInt(
      kRpcServerRequestCountMeasureName, kCount,
      "Number of server RPC request messages");
  return measure;
}

stats::MeasureInt RpcServerResponseCount() {
  static stats::MeasureInt measure = stats::MeasureRegistry::RegisterInt(
      kRpcServerResponseCountMeasureName, kCount,
      "Number of server RPC response messages");
  return measure;
}

}  // namespace opencensus
