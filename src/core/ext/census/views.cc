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

#include "src/core/ext/census/grpc_plugin.h"

#include "absl/time/time.h"
#include "opencensus/stats/internal/aggregation_window.h"
#include "opencensus/stats/internal/set_aggregation_window.h"
#include "opencensus/stats/stats.h"

namespace opencensus {

// These measure definitions should be kept in sync across opencensus
// implementations.

namespace {

stats::Aggregation BytesDistributionAggregation() {
  return stats::Aggregation::Distribution(stats::BucketBoundaries::Explicit(
      {0, 1024, 2048, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216,
       67108864, 268435456, 1073741824, 4294967296}));
}

stats::Aggregation MillisDistributionAggregation() {
  return stats::Aggregation::Distribution(stats::BucketBoundaries::Explicit(
      {0,   0.01, 0.05, 0.1,  0.3,   0.6,   0.8,   1,     2,   3,   4,
       5,   6,    8,    10,   13,    16,    20,    25,    30,  40,  50,
       65,  80,   100,  130,  160,   200,   250,   300,   400, 500, 650,
       800, 1000, 2000, 5000, 10000, 20000, 50000, 100000}));
}

stats::Aggregation CountDistributionAggregation() {
  return stats::Aggregation::Distribution(
      stats::BucketBoundaries::Exponential(17, 1.0, 2.0));
}

stats::ViewDescriptor MinuteDescriptor() {
  auto descriptor = stats::ViewDescriptor();
  SetAggregationWindow(stats::AggregationWindow::Interval(absl::Minutes(1)),
                       &descriptor);
  return descriptor;
}

stats::ViewDescriptor HourDescriptor() {
  auto descriptor = stats::ViewDescriptor();
  SetAggregationWindow(stats::AggregationWindow::Interval(absl::Hours(1)),
                       &descriptor);
  return descriptor;
}

}  // namespace

void RegisterGrpcViewsForExport() {
  ClientSentMessagesPerRpcCumulative().RegisterForExport();
  ClientSentBytesPerRpcCumulative().RegisterForExport();
  ClientReceivedMessagesPerRpcCumulative().RegisterForExport();
  ClientReceivedBytesPerRpcCumulative().RegisterForExport();
  ClientRoundtripLatencyCumulative().RegisterForExport();
  ClientServerLatencyCumulative().RegisterForExport();

  ServerSentMessagesPerRpcCumulative().RegisterForExport();
  ServerSentBytesPerRpcCumulative().RegisterForExport();
  ServerReceivedMessagesPerRpcCumulative().RegisterForExport();
  ServerReceivedBytesPerRpcCumulative().RegisterForExport();
  ServerServerLatencyCumulative().RegisterForExport();
}

// client cumulative
const stats::ViewDescriptor& ClientSentBytesPerRpcCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/client/sent_bytes_per_rpc/cumulative")
          .set_measure(kRpcClientSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientReceivedBytesPerRpcCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/client/received_bytes_per_rpc/cumulative")
          .set_measure(kRpcClientReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientRoundtripLatencyCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/client/roundtrip_latency/cumulative")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientServerLatencyCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/client/server_latency/cumulative")
          .set_measure(kRpcClientServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientCompletedRpcsCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/client/completed_rpcs/cumulative")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(stats::Aggregation::Count())
          .add_column(ClientMethodTagKey())
          .add_column(ClientStatusTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientSentMessagesPerRpcCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/client/received_messages_per_rpc/cumulative")
          .set_measure(kRpcClientSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientReceivedMessagesPerRpcCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/client/sent_messages_per_rpc/cumulative")
          .set_measure(kRpcClientReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

// server cumulative
const stats::ViewDescriptor& ServerSentBytesPerRpcCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/server/received_bytes_per_rpc/cumulative")
          .set_measure(kRpcServerSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerReceivedBytesPerRpcCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/server/sent_bytes_per_rpc/cumulative")
          .set_measure(kRpcServerReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerServerLatencyCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/server/elapsed_time/cumulative")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerCompletedRpcsCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/server/completed_rpcs/cumulative")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(stats::Aggregation::Count())
          .add_column(ServerMethodTagKey())
          .add_column(ServerStatusTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerSentMessagesPerRpcCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/server/received_messages_per_rpc/cumulative")
          .set_measure(kRpcServerSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerReceivedMessagesPerRpcCumulative() {
  const static stats::ViewDescriptor descriptor =
      stats::ViewDescriptor()
          .set_name("grpc.io/server/sent_messages_per_rpc/cumulative")
          .set_measure(kRpcServerReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

// client minute
const stats::ViewDescriptor& ClientSentBytesPerRpcMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/sent_bytes_per_rpc/minute")
          .set_measure(kRpcClientSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientReceivedBytesPerRpcMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/received_bytes_per_rpc/minute")
          .set_measure(kRpcClientReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientRoundtripLatencyMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/roundtrip_latency/minute")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientServerLatencyMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/server_latency/minute")
          .set_measure(kRpcClientServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientCompletedRpcsMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/completed_rpcs/minute")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(stats::Aggregation::Count())
          .add_column(ClientMethodTagKey())
          .add_column(ClientStatusTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientSentMessagesPerRpcMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/sent_messages_per_rpc/minute")
          .set_measure(kRpcClientSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientReceivedMessagesPerRpcMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/received_messages_per_rpc/minute")
          .set_measure(kRpcClientReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

// server minute
const stats::ViewDescriptor& ServerSentBytesPerRpcMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/sent_bytes_per_rpc/minute")
          .set_measure(kRpcServerSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerReceivedBytesPerRpcMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/received_bytes_per_rpc/minute")
          .set_measure(kRpcServerReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerServerLatencyMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/server_latency/minute")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerCompletedRpcsMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/completed_rpcs/minute")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(stats::Aggregation::Count())
          .add_column(ServerMethodTagKey())
          .add_column(ServerStatusTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerSentMessagesPerRpcMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/sent_messages_per_rpc/minute")
          .set_measure(kRpcServerSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerReceivedMessagesPerRpcMinute() {
  const static stats::ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/received_messages_per_rpc/minute")
          .set_measure(kRpcServerReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

// client hour
const stats::ViewDescriptor& ClientSentBytesPerRpcHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/sent_bytes_per_rpc/hour")
          .set_measure(kRpcClientSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientReceivedBytesPerRpcHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/received_bytes_per_rpc/hour")
          .set_measure(kRpcClientReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientRoundtripLatencyHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/roundtrip_latency/hour")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientServerLatencyHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/server_latency/hour")
          .set_measure(kRpcClientServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientCompletedRpcsHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/completed_rpcs/hour")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(stats::Aggregation::Count())
          .add_column(ClientMethodTagKey())
          .add_column(ClientStatusTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientSentMessagesPerRpcHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/sent_messages_per_rpc/hour")
          .set_measure(kRpcClientSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ClientReceivedMessagesPerRpcHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/received_messages_per_rpc/hour")
          .set_measure(kRpcClientReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

// server hour
const stats::ViewDescriptor& ServerSentBytesPerRpcHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/sent_bytes_per_rpc/hour")
          .set_measure(kRpcServerSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerReceivedBytesPerRpcHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/received_bytes_per_rpc/hour")
          .set_measure(kRpcServerReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerServerLatencyHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/server_latency/hour")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerCompletedRpcsHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/completed_rpcs/hour")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(stats::Aggregation::Count())
          .add_column(ServerMethodTagKey())
          .add_column(ServerStatusTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerSentMessagesPerRpcHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/sent_messages_per_rpc/hour")
          .set_measure(kRpcServerSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const stats::ViewDescriptor& ServerReceivedMessagesPerRpcHour() {
  const static stats::ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/received_messages_per_rpc/hour")
          .set_measure(kRpcServerReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

}  // namespace opencensus
