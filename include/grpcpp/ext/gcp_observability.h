//
// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef GRPCPP_EXT_GCP_OBSERVABILITY_H
#define GRPCPP_EXT_GCP_OBSERVABILITY_H

#include "absl/status/status.h"

namespace grpc {
namespace experimental {

// Initialize GCP Observability for gRPC.
// This should be called before any other gRPC operations like creating a
// channel, server, credentials etc.
// The most common usage would call this at the top (or near the top) in main().
// As an implementation detail, this properly initializes the OpenCensus stats
// and tracing plugin, so applications do not need to perform any additional
// gRPC C++ OpenCensus setup/registration to get GCP Observability for gRPC.
absl::Status GcpObservabilityInit();

// Gracefully shuts down GCP Observability.
// Note that graceful shutdown for stats and tracing is not yet supported.
void GcpObservabilityClose();

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_EXT_GCP_OBSERVABILITY_H
