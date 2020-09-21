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

#include "absl/strings/string_view.h"
#include "include/grpcpp/opencensus.h"
#include "opencensus/stats/stats.h"
#include "opencensus/tags/tag_key.h"

namespace grpc {

// The tag keys set when recording RPC stats.
::opencensus::tags::TagKey ClientMethodTagKey();
::opencensus::tags::TagKey ClientStatusTagKey();
::opencensus::tags::TagKey ServerMethodTagKey();
::opencensus::tags::TagKey ServerStatusTagKey();

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
const ::opencensus::stats::ViewDescriptor& ClientSentMessagesPerRpcCumulative();
const ::opencensus::stats::ViewDescriptor& ClientSentBytesPerRpcCumulative();
const ::opencensus::stats::ViewDescriptor&
ClientReceivedMessagesPerRpcCumulative();
const ::opencensus::stats::ViewDescriptor&
ClientReceivedBytesPerRpcCumulative();
const ::opencensus::stats::ViewDescriptor& ClientRoundtripLatencyCumulative();
const ::opencensus::stats::ViewDescriptor& ClientServerLatencyCumulative();
const ::opencensus::stats::ViewDescriptor& ClientCompletedRpcsCumulative();

const ::opencensus::stats::ViewDescriptor& ServerSentBytesPerRpcCumulative();
const ::opencensus::stats::ViewDescriptor&
ServerReceivedBytesPerRpcCumulative();
const ::opencensus::stats::ViewDescriptor& ServerServerLatencyCumulative();
const ::opencensus::stats::ViewDescriptor& ServerStartedCountCumulative();
const ::opencensus::stats::ViewDescriptor& ServerCompletedRpcsCumulative();
const ::opencensus::stats::ViewDescriptor& ServerSentMessagesPerRpcCumulative();
const ::opencensus::stats::ViewDescriptor&
ServerReceivedMessagesPerRpcCumulative();

const ::opencensus::stats::ViewDescriptor& ClientSentMessagesPerRpcMinute();
const ::opencensus::stats::ViewDescriptor& ClientSentBytesPerRpcMinute();
const ::opencensus::stats::ViewDescriptor& ClientReceivedMessagesPerRpcMinute();
const ::opencensus::stats::ViewDescriptor& ClientReceivedBytesPerRpcMinute();
const ::opencensus::stats::ViewDescriptor& ClientRoundtripLatencyMinute();
const ::opencensus::stats::ViewDescriptor& ClientServerLatencyMinute();
const ::opencensus::stats::ViewDescriptor& ClientCompletedRpcsMinute();

const ::opencensus::stats::ViewDescriptor& ServerSentMessagesPerRpcMinute();
const ::opencensus::stats::ViewDescriptor& ServerSentBytesPerRpcMinute();
const ::opencensus::stats::ViewDescriptor& ServerReceivedMessagesPerRpcMinute();
const ::opencensus::stats::ViewDescriptor& ServerReceivedBytesPerRpcMinute();
const ::opencensus::stats::ViewDescriptor& ServerServerLatencyMinute();
const ::opencensus::stats::ViewDescriptor& ServerCompletedRpcsMinute();

const ::opencensus::stats::ViewDescriptor& ClientSentMessagesPerRpcHour();
const ::opencensus::stats::ViewDescriptor& ClientSentBytesPerRpcHour();
const ::opencensus::stats::ViewDescriptor& ClientReceivedMessagesPerRpcHour();
const ::opencensus::stats::ViewDescriptor& ClientReceivedBytesPerRpcHour();
const ::opencensus::stats::ViewDescriptor& ClientRoundtripLatencyHour();
const ::opencensus::stats::ViewDescriptor& ClientServerLatencyHour();
const ::opencensus::stats::ViewDescriptor& ClientCompletedRpcsHour();

const ::opencensus::stats::ViewDescriptor& ServerSentMessagesPerRpcHour();
const ::opencensus::stats::ViewDescriptor& ServerSentBytesPerRpcHour();
const ::opencensus::stats::ViewDescriptor& ServerReceivedMessagesPerRpcHour();
const ::opencensus::stats::ViewDescriptor& ServerReceivedBytesPerRpcHour();
const ::opencensus::stats::ViewDescriptor& ServerServerLatencyHour();
const ::opencensus::stats::ViewDescriptor& ServerStartedCountHour();
const ::opencensus::stats::ViewDescriptor& ServerCompletedRpcsHour();

}  // namespace grpc

#endif /* GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_GRPC_PLUGIN_H */
