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

#include "src/cpp/ext/filters/census/measures.h"

#include "opencensus/stats/stats.h"

#include "src/cpp/ext/filters/census/grpc_plugin.h"

namespace grpc {

using ::opencensus::stats::MeasureDouble;
using ::opencensus::stats::MeasureInt64;

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
MeasureDouble RpcClientSentBytesPerRpc() {
  static const auto measure = MeasureDouble::Register(
      kRpcClientSentBytesPerRpcMeasureName,
      "Total bytes sent across all request messages per RPC", kUnitBytes);
  return measure;
}

MeasureDouble RpcClientReceivedBytesPerRpc() {
  static const auto measure = MeasureDouble::Register(
      kRpcClientReceivedBytesPerRpcMeasureName,
      "Total bytes received across all response messages per RPC", kUnitBytes);
  return measure;
}

MeasureDouble RpcClientRoundtripLatency() {
  static const auto measure = MeasureDouble::Register(
      kRpcClientRoundtripLatencyMeasureName,
      "Time between first byte of request sent to last byte of response "
      "received, or terminal error",
      kUnitMilliseconds);
  return measure;
}

MeasureDouble RpcClientServerLatency() {
  static const auto measure = MeasureDouble::Register(
      kRpcClientServerLatencyMeasureName,
      "Time between first byte of request received to last byte of response "
      "sent, or terminal error (propagated from the server)",
      kUnitMilliseconds);
  return measure;
}

MeasureInt64 RpcClientSentMessagesPerRpc() {
  static const auto measure =
      MeasureInt64::Register(kRpcClientSentMessagesPerRpcMeasureName,
                             "Number of messages sent per RPC", kCount);
  return measure;
}

MeasureInt64 RpcClientReceivedMessagesPerRpc() {
  static const auto measure =
      MeasureInt64::Register(kRpcClientReceivedMessagesPerRpcMeasureName,
                             "Number of messages received per RPC", kCount);
  return measure;
}

MeasureInt64 RpcClientStartedRpcs() {
  static const auto measure =
      MeasureInt64::Register(kRpcClientStartedRpcsMeasureName,
                             "The total number of client RPCs ever opened, "
                             "including those that have not been completed.",
                             kCount);
  return measure;
}

// Client per-overall-client-call measures
MeasureInt64 RpcClientRetriesPerCall() {
  static const auto measure =
      MeasureInt64::Register(kRpcClientRetriesPerCallMeasureName,
                             "Number of retry or hedging attempts excluding "
                             "transparent retries made during the client call",
                             kCount);
  return measure;
}

MeasureInt64 RpcClientTransparentRetriesPerCall() {
  static const auto measure = MeasureInt64::Register(
      kRpcClientTransparentRetriesPerCallMeasureName,
      "Number of transparent retries made during the client call", kCount);
  return measure;
}

MeasureDouble RpcClientRetryDelayPerCall() {
  static const auto measure =
      MeasureDouble::Register(kRpcClientRetryDelayPerCallMeasureName,
                              "Total time of delay while there is no active "
                              "attempt during the client call",
                              kUnitMilliseconds);
  return measure;
}

// Server
MeasureDouble RpcServerSentBytesPerRpc() {
  static const auto measure = MeasureDouble::Register(
      kRpcServerSentBytesPerRpcMeasureName,
      "Total bytes sent across all messages per RPC", kUnitBytes);
  return measure;
}

MeasureDouble RpcServerReceivedBytesPerRpc() {
  static const auto measure = MeasureDouble::Register(
      kRpcServerReceivedBytesPerRpcMeasureName,
      "Total bytes received across all messages per RPC", kUnitBytes);
  return measure;
}

MeasureDouble RpcServerServerLatency() {
  static const auto measure = MeasureDouble::Register(
      kRpcServerServerLatencyMeasureName,
      "Time between first byte of request received to last byte of response "
      "sent, or terminal error",
      kUnitMilliseconds);
  return measure;
}

MeasureInt64 RpcServerStartedRpcs() {
  static const auto measure =
      MeasureInt64::Register(kRpcServerStartedRpcsMeasureName,
                             "The total number of server RPCs ever opened, "
                             "including those that have not been completed.",
                             kCount);
  return measure;
}

MeasureInt64 RpcServerSentMessagesPerRpc() {
  static const auto measure =
      MeasureInt64::Register(kRpcServerSentMessagesPerRpcMeasureName,
                             "Number of messages sent per RPC", kCount);
  return measure;
}

MeasureInt64 RpcServerReceivedMessagesPerRpc() {
  static const auto measure =
      MeasureInt64::Register(kRpcServerReceivedMessagesPerRpcMeasureName,
                             "Number of messages received per RPC", kCount);
  return measure;
}

}  // namespace grpc
