

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

#include <grpcpp/ext/server_load_reporting.h>

#include "src/core/ext/filters/load_reporting/registered_opencensus_objects.h"
#include "src/core/ext/filters/load_reporting/server_load_reporting_filter.h"
#include "src/cpp/server/load_reporter/load_reporting_service_server_builder_plugin.h"

namespace grpc {
namespace load_reporter {

void EnableServerLoadReporting() {
  static bool enabled = false;
  if (enabled) return;
  // Register the filter.
  RegisterChannelFilter<ServerLoadReportingChannelData,
                        ServerLoadReportingCallData>(
      "server_load_reporting", GRPC_SERVER_CHANNEL, INT_MAX, nullptr);
  // Add server builder plugin to set up the service.
  ServerBuilder::InternalAddPluginFactory(
      &load_reporter::CreateLoadReportingServiceServerBuilderPlugin);
  // Access measures to ensure they are initialized. Otherwise, we can't create
  // any valid view before the first RPC.
  load_reporter::MeasureStartCount();
  load_reporter::MeasureEndCount();
  load_reporter::MeasureEndBytesSent();
  load_reporter::MeasureEndBytesReceived();
  load_reporter::MeasureEndLatencyMs();
  load_reporter::MeasureOtherCallMetric();
  enabled = true;
}

}  // namespace load_reporter
}  // namespace grpc
