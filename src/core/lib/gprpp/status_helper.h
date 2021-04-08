//
//
// Copyright 2021 the gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPRPP_STATUS_HELPER_H
#define GRPC_CORE_LIB_GPRPP_STATUS_HELPER_H

#include <grpc/support/port_platform.h>

#include "absl/status/status.h"
#include "google/rpc/status.upb.h"

#include "src/core/lib/gprpp/debug_location.h"

namespace grpc_core {

/// Creates a status with given additional information
absl::Status StatusCreate(
    absl::StatusCode code, absl::string_view msg, const DebugLocation& location,
    std::initializer_list<absl::Status> children) GRPC_MUST_USE_RESULT;

/// Sets the int property to the status
void StatusSetInt(absl::Status* status, absl::string_view field,
                  intptr_t value);

/// Gets the int property from the status
absl::optional<intptr_t> StatusGetInt(
    const absl::Status& status, absl::string_view field) GRPC_MUST_USE_RESULT;

/// Sets the str property to the status
void StatusSetStr(absl::Status* status, absl::string_view field,
                  absl::string_view value);

/// Gets the str property from the status
absl::optional<std::string> StatusGetStr(
    const absl::Status& status, absl::string_view field) GRPC_MUST_USE_RESULT;

/// Adds a child status to status
void StatusAddChild(absl::Status* status, absl::Status child);

/// Returns all children status from a status
std::vector<absl::Status> StatusGetChildren(absl::Status status)
    GRPC_MUST_USE_RESULT;

/// Builds a upb message, google_rpc_Status from a status
google_rpc_Status* StatusToProto(absl::Status status,
                                 upb_arena* arena) GRPC_MUST_USE_RESULT;

/// Build a status from a upb message, google_rpc_Status
absl::Status StatusFromProto(google_rpc_Status* msg) GRPC_MUST_USE_RESULT;

/// Returns a string representation from status
/// Error status will be like
///   STATUS[:MESSAGE] [{PAYLOADS[, children:[CHILDREN-STATUS-LISTS]]}]
/// e.g.
///   CANCELLATION:SampleMessage {errno:'2021', line:'54', children:[ABORTED]}
std::string StatusToString(const absl::Status& status) GRPC_MUST_USE_RESULT;

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_STATUS_HELPER_H
