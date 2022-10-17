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

#ifndef GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_GRPC_PLUGIN_H
#define GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_GRPC_PLUGIN_H

#include <grpc/support/port_platform.h>

#include <grpcpp/opencensus.h>

namespace grpc {

// The tag keys set when recording RPC stats.
using experimental::ClientMethodTagKey;
using experimental::ClientStatusTagKey;
using experimental::ServerMethodTagKey;
using experimental::ServerStatusTagKey;

// Names of measures used by the plugin--users can create views on these
// measures but should not record data for them.
using experimental::kRpcClientReceivedBytesPerRpcMeasureName;
using experimental::kRpcClientReceivedMessagesPerRpcMeasureName;
using experimental::kRpcClientRetriesPerCallMeasureName;
using experimental::kRpcClientRetryDelayPerCallMeasureName;
using experimental::kRpcClientRoundtripLatencyMeasureName;
using experimental::kRpcClientSentBytesPerRpcMeasureName;
using experimental::kRpcClientSentMessagesPerRpcMeasureName;
using experimental::kRpcClientServerLatencyMeasureName;
using experimental::kRpcClientStartedRpcsMeasureName;
using experimental::kRpcClientTransparentRetriesPerCallMeasureName;

using experimental::kRpcServerReceivedBytesPerRpcMeasureName;
using experimental::kRpcServerReceivedMessagesPerRpcMeasureName;
using experimental::kRpcServerSentBytesPerRpcMeasureName;
using experimental::kRpcServerSentMessagesPerRpcMeasureName;
using experimental::kRpcServerServerLatencyMeasureName;
using experimental::kRpcServerStartedRpcsMeasureName;

// Canonical gRPC view definitions.
using experimental::ClientCompletedRpcsCumulative;
using experimental::ClientReceivedBytesPerRpcCumulative;
using experimental::ClientReceivedMessagesPerRpcCumulative;
using experimental::ClientRetriesCumulative;
using experimental::ClientRetriesPerCallCumulative;
using experimental::ClientRetryDelayPerCallCumulative;
using experimental::ClientRoundtripLatencyCumulative;
using experimental::ClientSentBytesPerRpcCumulative;
using experimental::ClientSentMessagesPerRpcCumulative;
using experimental::ClientServerLatencyCumulative;
using experimental::ClientStartedRpcsCumulative;
using experimental::ClientTransparentRetriesCumulative;
using experimental::ClientTransparentRetriesPerCallCumulative;

using experimental::ServerCompletedRpcsCumulative;
using experimental::ServerReceivedBytesPerRpcCumulative;
using experimental::ServerReceivedMessagesPerRpcCumulative;
using experimental::ServerSentBytesPerRpcCumulative;
using experimental::ServerSentMessagesPerRpcCumulative;
using experimental::ServerServerLatencyCumulative;
using experimental::ServerStartedCountCumulative;
using experimental::ServerStartedRpcsCumulative;

using experimental::ClientCompletedRpcsMinute;
using experimental::ClientReceivedBytesPerRpcMinute;
using experimental::ClientReceivedMessagesPerRpcMinute;
using experimental::ClientRetriesMinute;
using experimental::ClientRetriesPerCallMinute;
using experimental::ClientRetryDelayPerCallMinute;
using experimental::ClientRoundtripLatencyMinute;
using experimental::ClientSentBytesPerRpcMinute;
using experimental::ClientSentMessagesPerRpcMinute;
using experimental::ClientServerLatencyMinute;
using experimental::ClientStartedRpcsMinute;
using experimental::ClientTransparentRetriesMinute;
using experimental::ClientTransparentRetriesPerCallMinute;

using experimental::ServerCompletedRpcsMinute;
using experimental::ServerReceivedBytesPerRpcMinute;
using experimental::ServerReceivedMessagesPerRpcMinute;
using experimental::ServerSentBytesPerRpcMinute;
using experimental::ServerSentMessagesPerRpcMinute;
using experimental::ServerServerLatencyMinute;
using experimental::ServerStartedRpcsMinute;

using experimental::ClientCompletedRpcsHour;
using experimental::ClientReceivedBytesPerRpcHour;
using experimental::ClientReceivedMessagesPerRpcHour;
using experimental::ClientRetriesHour;
using experimental::ClientRetriesPerCallHour;
using experimental::ClientRetryDelayPerCallHour;
using experimental::ClientRoundtripLatencyHour;
using experimental::ClientSentBytesPerRpcHour;
using experimental::ClientSentMessagesPerRpcHour;
using experimental::ClientServerLatencyHour;
using experimental::ClientStartedRpcsHour;
using experimental::ClientTransparentRetriesHour;
using experimental::ClientTransparentRetriesPerCallHour;

using experimental::ServerCompletedRpcsHour;
using experimental::ServerReceivedBytesPerRpcHour;
using experimental::ServerReceivedMessagesPerRpcHour;
using experimental::ServerSentBytesPerRpcHour;
using experimental::ServerSentMessagesPerRpcHour;
using experimental::ServerServerLatencyHour;
using experimental::ServerStartedCountHour;
using experimental::ServerStartedRpcsHour;

// Enables/Disables OpenCensus stats/tracing. It's only safe to do at the start
// of a program, before any channels/servers are built.
void EnableOpenCensusStats(bool enable);
void EnableOpenCensusTracing(bool enable);
// Gets the current status of OpenCensus stats/tracing
bool OpenCensusStatsEnabled();
bool OpenCensusTracingEnabled();

}  // namespace grpc

#endif /* GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_GRPC_PLUGIN_H */
