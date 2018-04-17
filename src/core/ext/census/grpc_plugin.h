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
#include "opencensus/stats/stats.h"
#include "opencensus/trace/span.h"

namespace grpc {
class ServerContext;
}

namespace opencensus {

// Registers the OpenCensus plugin with gRPC, so that it will be used for future
// RPCs. This must be called before any views are created on the measures
// defined below.
void RegisterGrpcPlugin();

// RPC stats definitions, defined by
// https://github.com/census-instrumentation/opencensus-specs/blob/master/stats/gRPC.md

// Registers the cumulative gRPC views so that they will be exported by any
// registered stats exporter.
// For on-task stats, construct a View using the ViewDescriptors below.
void RegisterGrpcViewsForExport();

// Returns the tracing Span for the current RPC.
opencensus::trace::Span GetSpanFromServerContext(grpc::ServerContext* context);

// The tag keys set when recording RPC stats.
opencensus::stats::TagKey ClientMethodTagKey();
opencensus::stats::TagKey ClientStatusTagKey();
opencensus::stats::TagKey ServerMethodTagKey();
opencensus::stats::TagKey ServerStatusTagKey();

// Names of measures used by the plugin--users can create views on these
// measures but should not record data for them.
extern const absl::string_view kRpcClientSentMessagesPerRpcMeasureName;
extern const absl::string_view kRpcClientSentBytesPerRpcMeasureName;
extern const absl::string_view kRpcClientReceivedMessagesPerRpcMeasureName;
extern const absl::string_view kRpcClientReceivedBytesPerRpcMeasureName;
extern const absl::string_view kRpcClientRoundtripLatencyMeasureName;
extern const absl::string_view kRpcClientServerLatencyMeasureName;

extern const absl::string_view kRpcServerSentMessagesPerRpcMeasureName;
extern const absl::string_view kRpcServerSentBytesPerRpcMeasureName;
extern const absl::string_view kRpcServerReceivedMessagesPerRpcMeasureName;
extern const absl::string_view kRpcServerReceivedBytesPerRpcMeasureName;
extern const absl::string_view kRpcServerServerLatencyMeasureName;

// Canonical gRPC view definitions.
const stats::ViewDescriptor& ClientSentMessagesPerRpcCumulative();
const stats::ViewDescriptor& ClientSentBytesPerRpcCumulative();
const stats::ViewDescriptor& ClientReceivedMessagesPerRpcCumulative();
const stats::ViewDescriptor& ClientReceivedBytesPerRpcCumulative();
const stats::ViewDescriptor& ClientRoundtripLatencyCumulative();
const stats::ViewDescriptor& ClientServerLatencyCumulative();
const stats::ViewDescriptor& ClientCompletedRpcsCumulative();

const stats::ViewDescriptor& ServerSentBytesPerRpcCumulative();
const stats::ViewDescriptor& ServerReceivedBytesPerRpcCumulative();
const stats::ViewDescriptor& ServerServerLatencyCumulative();
const stats::ViewDescriptor& ServerStartedCountCumulative();
const stats::ViewDescriptor& ServerCompletedRpcsCumulative();
const stats::ViewDescriptor& ServerSentMessagesPerRpcCumulative();
const stats::ViewDescriptor& ServerReceivedMessagesPerRpcCumulative();

const stats::ViewDescriptor& ClientSentMessagesPerRpcMinute();
const stats::ViewDescriptor& ClientSentBytesPerRpcMinute();
const stats::ViewDescriptor& ClientReceivedMessagesPerRpcMinute();
const stats::ViewDescriptor& ClientReceivedBytesPerRpcMinute();
const stats::ViewDescriptor& ClientRoundtripLatencyMinute();
const stats::ViewDescriptor& ClientServerLatencyMinute();
const stats::ViewDescriptor& ClientCompletedRpcsMinute();

const stats::ViewDescriptor& ServerSentMessagesPerRpcMinute();
const stats::ViewDescriptor& ServerSentBytesPerRpcMinute();
const stats::ViewDescriptor& ServerReceivedMessagesPerRpcMinute();
const stats::ViewDescriptor& ServerReceivedBytesPerRpcMinute();
const stats::ViewDescriptor& ServerServerLatencyMinute();
const stats::ViewDescriptor& ServerCompletedRpcsMinute();

const stats::ViewDescriptor& ClientSentMessagesPerRpcHour();
const stats::ViewDescriptor& ClientSentBytesPerRpcHour();
const stats::ViewDescriptor& ClientReceivedMessagesPerRpcHour();
const stats::ViewDescriptor& ClientReceivedBytesPerRpcHour();
const stats::ViewDescriptor& ClientRoundtripLatencyHour();
const stats::ViewDescriptor& ClientServerLatencyHour();
const stats::ViewDescriptor& ClientCompletedRpcsHour();

const stats::ViewDescriptor& ServerSentMessagesPerRpcHour();
const stats::ViewDescriptor& ServerSentBytesPerRpcHour();
const stats::ViewDescriptor& ServerReceivedMessagesPerRpcHour();
const stats::ViewDescriptor& ServerReceivedBytesPerRpcHour();
const stats::ViewDescriptor& ServerServerLatencyHour();
const stats::ViewDescriptor& ServerStartedCountHour();
const stats::ViewDescriptor& ServerCompletedRpcsHour();

}  // namespace opencensus

#endif /* GRPC_CORE_EXT_CENSUS_GRPC_PLUGIN_H */
