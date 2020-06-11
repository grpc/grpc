/*
 *
 * Copyright 2019 gRPC authors.
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

#ifndef GRPCPP_OPENCENSUS_H
#define GRPCPP_OPENCENSUS_H

#include "opencensus/trace/span.h"

namespace grpc {
class ServerContext;
// These symbols in this file will not be included in the binary unless
// grpc_opencensus_plugin build target was added as a dependency. At the moment
// it is only setup to be built with Bazel.

// Registers the OpenCensus plugin with gRPC, so that it will be used for future
// RPCs. This must be called before any views are created.
void RegisterOpenCensusPlugin();

// RPC stats definitions, defined by
// https://github.com/census-instrumentation/opencensus-specs/blob/master/stats/gRPC.md

// Registers the cumulative gRPC views so that they will be exported by any
// registered stats exporter. For on-task stats, construct a View using the
// ViewDescriptors below.
void RegisterOpenCensusViewsForExport();

// Returns the tracing Span for the current RPC.
::opencensus::trace::Span GetSpanFromServerContext(ServerContext* context);

}  // namespace grpc

#endif  // GRPCPP_OPENCENSUS_H
