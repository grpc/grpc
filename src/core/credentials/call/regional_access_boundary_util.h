//
//
// Copyright 2015 gRPC authors.
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
//

#ifndef GRPC_SRC_CORE_CREDENTIALS_CALL_REGIONAL_ACCESS_BOUNDARY_UTIL_H
#define GRPC_SRC_CORE_CREDENTIALS_CALL_REGIONAL_ACCESS_BOUNDARY_UTIL_H

#include "absl/status/statusor.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/call/metadata.h"


namespace grpc_core {

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>> FetchRegionalAccessBoundary(
    grpc_core::RefCountedPtr<grpc_call_credentials> creds,
    grpc_core::ClientMetadataHandle initial_metadata);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_REGIONAL_ACCESS_BOUNDARY_UTIL_H