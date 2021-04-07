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

#ifndef GRPC_CORE_LIB_IOMGR_STATUS_UTIL_H
#define GRPC_CORE_LIB_IOMGR_STATUS_UTIL_H

#include <grpc/support/port_platform.h>

#include "absl/status/status.h"

#include "src/core/lib/iomgr/error.h"

extern "C" struct google_rpc_Status;
extern "C" struct upb_arena;

namespace grpc_core {

/// Creates a status with given additional information
absl::Status StatusCreate(
    absl::StatusCode code, absl::string_view msg, const char* file, int line,
    std::initializer_list<absl::Status> children) GRPC_MUST_USE_RESULT;

/// Sets the grpc_error_ints property to the status
void StatusSetInt(absl::Status* status, grpc_error_ints which, intptr_t value);

/// Gets the grpc_error_ints property from the status
absl::optional<intptr_t> StatusGetInt(
    const absl::Status& status, grpc_error_ints which) GRPC_MUST_USE_RESULT;

/// Sets the grpc_error_strs property to the status
void StatusSetStr(absl::Status* status, grpc_error_strs which,
                  std::string value);

/// Gets the grpc_error_strs property from the status
absl::optional<std::string> StatusGetStr(
    const absl::Status& status, grpc_error_strs which) GRPC_MUST_USE_RESULT;

/// Adds a child status to status
void StatusAddChild(absl::Status* status, absl::Status child);

/// Returns all children status from a status
std::vector<absl::Status> StatusGetChildren(absl::Status status);

/// Builds a upb message, google_rpc_Status from a status
google_rpc_Status* StatusToProto(absl::Status status, upb_arena* arena);

/// Build a status from a upb message, google_rpc_Status
absl::Status StatusFromProto(google_rpc_Status* msg);

/// Returns a string representation from status
/// Error status will be like
///   STATUS[:MESSAGE] [{PAYLOADS[, children:[CHILDREN-STATUS-LISTS]]}]
/// e.g.
///   CANCELLATION:SampleMessage {errno:'2021', line:'54', children:[ABORTED]}
std::string StatusToString(const absl::Status& status) GRPC_MUST_USE_RESULT;

// -------------
// Create helper

/// Create an OS error status with given additional information
absl::Status StatusCreateOS(const char* file, int line, int err,
                            const char* call_name) GRPC_MUST_USE_RESULT;

#ifdef GPR_WINDOWS
/// Create a WSA error status with given additional information
absl::Status StatusCreateWSA(const char* file, int line, int err,
                             const char* call_name) GRPC_MUST_USE_RESULT;
#endif  // #ifdef GPR_WINDOWS

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_IOMGR_STATUS_UTIL_H
