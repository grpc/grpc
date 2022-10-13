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
#include "opencensus/stats/stats.h"
#include "opencensus/tags/tag_key.h"

#include <grpcpp/opencensus.h>

namespace grpc {

// Enables/Disables OpenCensus stats/tracing. It's only safe to do at the start
// of a program, before any channels/servers are built.
void EnableOpenCensusStats(bool enable);
void EnableOpenCensusTracing(bool enable);
// Gets the current status of OpenCensus stats/tracing
bool OpenCensusStatsEnabled();
bool OpenCensusTracingEnabled();

}  // namespace grpc

#endif /* GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_GRPC_PLUGIN_H */
