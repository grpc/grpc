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

#ifndef GRPC_INTERNAL_CPP_EXT_GCP_OBSERVABILITY_GCP_OBSERVABILITY_H
#define GRPC_INTERNAL_CPP_EXT_GCP_OBSERVABILITY_GCP_OBSERVABILITY_H

// TODO(yashykt): This file would have been in the top include/grpcpp directory
// instead of in src/, but I'm not yet sure about the naming, so keeping it here
// till we decide.

#include "absl/status/status.h"

namespace grpc {
namespace experimental {

// Initialize GCP Observability for gRPC.
absl::Status GcpObservabilityInit();

}  // namespace experimental
}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_EXT_GCP_OBSERVABILITY_GCP_OBSERVABILITY_H
