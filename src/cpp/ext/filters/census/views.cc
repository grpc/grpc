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

#include "absl/time/time.h"
#include "opencensus/stats/internal/aggregation_window.h"
#include "opencensus/stats/internal/set_aggregation_window.h"
#include "opencensus/stats/stats.h"

namespace grpc {

using ::opencensus::stats::Aggregation;
using ::opencensus::stats::AggregationWindow;
using ::opencensus::stats::BucketBoundaries;
using ::opencensus::stats::ViewDescriptor;

// These measure definitions should be kept in sync across opencensus
// implementations.

namespace {

Aggregation BytesDistributionAggregation() {
  return Aggregation::Distribution(BucketBoundaries::Explicit(
      {0, 1024, 2048, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216,
       67108864, 268435456, 1073741824, 4294967296}));
}

Aggregation MillisDistributionAggregation() {
  return Aggregation::Distribution(BucketBoundaries::Explicit(
      {0,   0.01, 0.05, 0.1,  0.3,   0.6,   0.8,   1,     2,   3,   4,
       5,   6,    8,    10,   13,    16,    20,    25,    30,  40,  50,
       65,  80,   100,  130,  160,   200,   250,   300,   400, 500, 650,
       800, 1000, 2000, 5000, 10000, 20000, 50000, 100000}));
}

Aggregation CountDistributionAggregation() {
  return Aggregation::Distribution(BucketBoundaries::Exponential(17, 1.0, 2.0));
}

ViewDescriptor MinuteDescriptor() {
  auto descriptor = ViewDescriptor();
  SetAggregationWindow(AggregationWindow::Interval(absl::Minutes(1)),
                       &descriptor);
  return descriptor;
}

ViewDescriptor HourDescriptor() {
  auto descriptor = ViewDescriptor();
  SetAggregationWindow(AggregationWindow::Interval(absl::Hours(1)),
                       &descriptor);
  return descriptor;
}

}  // namespace

void RegisterOpenCensusViewsForExport() {
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
const ViewDescriptor& ClientSentBytesPerRpcCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/client/sent_bytes_per_rpc/cumulative")
          .set_measure(kRpcClientSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientReceivedBytesPerRpcCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/client/received_bytes_per_rpc/cumulative")
          .set_measure(kRpcClientReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientRoundtripLatencyCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/client/roundtrip_latency/cumulative")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientServerLatencyCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/client/server_latency/cumulative")
          .set_measure(kRpcClientServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientCompletedRpcsCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/client/completed_rpcs/cumulative")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(Aggregation::Count())
          .add_column(ClientMethodTagKey())
          .add_column(ClientStatusTagKey());
  return descriptor;
}

const ViewDescriptor& ClientSentMessagesPerRpcCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/client/received_messages_per_rpc/cumulative")
          .set_measure(kRpcClientSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientReceivedMessagesPerRpcCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/client/sent_messages_per_rpc/cumulative")
          .set_measure(kRpcClientReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

// server cumulative
const ViewDescriptor& ServerSentBytesPerRpcCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/server/received_bytes_per_rpc/cumulative")
          .set_measure(kRpcServerSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerReceivedBytesPerRpcCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/server/sent_bytes_per_rpc/cumulative")
          .set_measure(kRpcServerReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerServerLatencyCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/server/elapsed_time/cumulative")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerCompletedRpcsCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/server/completed_rpcs/cumulative")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(Aggregation::Count())
          .add_column(ServerMethodTagKey())
          .add_column(ServerStatusTagKey());
  return descriptor;
}

const ViewDescriptor& ServerSentMessagesPerRpcCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/server/received_messages_per_rpc/cumulative")
          .set_measure(kRpcServerSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerReceivedMessagesPerRpcCumulative() {
  const static ViewDescriptor descriptor =
      ViewDescriptor()
          .set_name("grpc.io/server/sent_messages_per_rpc/cumulative")
          .set_measure(kRpcServerReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

// client minute
const ViewDescriptor& ClientSentBytesPerRpcMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/sent_bytes_per_rpc/minute")
          .set_measure(kRpcClientSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientReceivedBytesPerRpcMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/received_bytes_per_rpc/minute")
          .set_measure(kRpcClientReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientRoundtripLatencyMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/roundtrip_latency/minute")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientServerLatencyMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/server_latency/minute")
          .set_measure(kRpcClientServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientCompletedRpcsMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/completed_rpcs/minute")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(Aggregation::Count())
          .add_column(ClientMethodTagKey())
          .add_column(ClientStatusTagKey());
  return descriptor;
}

const ViewDescriptor& ClientSentMessagesPerRpcMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/sent_messages_per_rpc/minute")
          .set_measure(kRpcClientSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientReceivedMessagesPerRpcMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/client/received_messages_per_rpc/minute")
          .set_measure(kRpcClientReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

// server minute
const ViewDescriptor& ServerSentBytesPerRpcMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/sent_bytes_per_rpc/minute")
          .set_measure(kRpcServerSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerReceivedBytesPerRpcMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/received_bytes_per_rpc/minute")
          .set_measure(kRpcServerReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerServerLatencyMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/server_latency/minute")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerCompletedRpcsMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/completed_rpcs/minute")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(Aggregation::Count())
          .add_column(ServerMethodTagKey())
          .add_column(ServerStatusTagKey());
  return descriptor;
}

const ViewDescriptor& ServerSentMessagesPerRpcMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/sent_messages_per_rpc/minute")
          .set_measure(kRpcServerSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerReceivedMessagesPerRpcMinute() {
  const static ViewDescriptor descriptor =
      MinuteDescriptor()
          .set_name("grpc.io/server/received_messages_per_rpc/minute")
          .set_measure(kRpcServerReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

// client hour
const ViewDescriptor& ClientSentBytesPerRpcHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/sent_bytes_per_rpc/hour")
          .set_measure(kRpcClientSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientReceivedBytesPerRpcHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/received_bytes_per_rpc/hour")
          .set_measure(kRpcClientReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientRoundtripLatencyHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/roundtrip_latency/hour")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientServerLatencyHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/server_latency/hour")
          .set_measure(kRpcClientServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientCompletedRpcsHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/completed_rpcs/hour")
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_aggregation(Aggregation::Count())
          .add_column(ClientMethodTagKey())
          .add_column(ClientStatusTagKey());
  return descriptor;
}

const ViewDescriptor& ClientSentMessagesPerRpcHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/sent_messages_per_rpc/hour")
          .set_measure(kRpcClientSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ClientReceivedMessagesPerRpcHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/client/received_messages_per_rpc/hour")
          .set_measure(kRpcClientReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ClientMethodTagKey());
  return descriptor;
}

// server hour
const ViewDescriptor& ServerSentBytesPerRpcHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/sent_bytes_per_rpc/hour")
          .set_measure(kRpcServerSentBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerReceivedBytesPerRpcHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/received_bytes_per_rpc/hour")
          .set_measure(kRpcServerReceivedBytesPerRpcMeasureName)
          .set_aggregation(BytesDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerServerLatencyHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/server_latency/hour")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(MillisDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerCompletedRpcsHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/completed_rpcs/hour")
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_aggregation(Aggregation::Count())
          .add_column(ServerMethodTagKey())
          .add_column(ServerStatusTagKey());
  return descriptor;
}

const ViewDescriptor& ServerSentMessagesPerRpcHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/sent_messages_per_rpc/hour")
          .set_measure(kRpcServerSentMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

const ViewDescriptor& ServerReceivedMessagesPerRpcHour() {
  const static ViewDescriptor descriptor =
      HourDescriptor()
          .set_name("grpc.io/server/received_messages_per_rpc/hour")
          .set_measure(kRpcServerReceivedMessagesPerRpcMeasureName)
          .set_aggregation(CountDistributionAggregation())
          .add_column(ServerMethodTagKey());
  return descriptor;
}

}  // namespace grpc
