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
stats::MeasureDouble RpcClientSentBytesPerRpc() {
  static const auto measure = stats::MeasureDouble::Register(
      kRpcClientSentBytesPerRpcMeasureName,
      "Total bytes sent across all request messages per RPC", kUnitBytes);
  return measure;
}

stats::MeasureDouble RpcClientReceivedBytesPerRpc() {
  static const auto measure = stats::MeasureDouble::Register(
      kRpcClientReceivedBytesPerRpcMeasureName,
      "Total bytes received across all response messages per RPC", kUnitBytes);
  return measure;
}

stats::MeasureDouble RpcClientRoundtripLatency() {
  static const auto measure = stats::MeasureDouble::Register(
      kRpcClientRoundtripLatencyMeasureName,
      "Time between first byte of request sent to last byte of response "
      "received, or terminal error",
      kUnitMilliseconds);
  return measure;
}

stats::MeasureDouble RpcClientServerLatency() {
  static const auto measure = stats::MeasureDouble::Register(
      kRpcClientServerLatencyMeasureName,
      "Time between first byte of request received to last byte of response "
      "sent, or terminal error (propagated from the server)",
      kUnitMilliseconds);
  return measure;
}

stats::MeasureInt64 RpcClientSentMessagesPerRpc() {
  static const auto measure =
      stats::MeasureInt64::Register(kRpcClientSentMessagesPerRpcMeasureName,
                                    "Number of messages sent per RPC", kCount);
  return measure;
}

stats::MeasureInt64 RpcClientReceivedMessagesPerRpc() {
  static const auto measure = stats::MeasureInt64::Register(
      kRpcClientReceivedMessagesPerRpcMeasureName,
      "Number of messages received per RPC", kCount);
  return measure;
}

// Server
stats::MeasureDouble RpcServerSentBytesPerRpc() {
  static const auto measure = stats::MeasureDouble::Register(
      kRpcServerSentBytesPerRpcMeasureName,
      "Total bytes sent across all messages per RPC", kUnitBytes);
  return measure;
}

stats::MeasureDouble RpcServerReceivedBytesPerRpc() {
  static const auto measure = stats::MeasureDouble::Register(
      kRpcServerReceivedBytesPerRpcMeasureName,
      "Total bytes received across all messages per RPC", kUnitBytes);
  return measure;
}

stats::MeasureDouble RpcServerServerLatency() {
  static const auto measure = stats::MeasureDouble::Register(
      kRpcServerServerLatencyMeasureName,
      "Time between first byte of request received to last byte of response "
      "sent, or terminal error",
      kUnitMilliseconds);
  return measure;
}

stats::MeasureInt64 RpcServerSentMessagesPerRpc() {
  static const auto measure =
      stats::MeasureInt64::Register(kRpcServerSentMessagesPerRpcMeasureName,
                                    "Number of messages sent per RPC", kCount);
  return measure;
}

stats::MeasureInt64 RpcServerReceivedMessagesPerRpc() {
  static const auto measure = stats::MeasureInt64::Register(
      kRpcServerReceivedMessagesPerRpcMeasureName,
      "Number of messages received per RPC", kCount);
  return measure;
}

}  // namespace opencensus
