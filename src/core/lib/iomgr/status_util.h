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

namespace grpc_core {

/// Create a status with given additional information
absl::Status grpc_status_create(
    absl::StatusCode code, absl::string_view msg, const char* file, int line,
    std::initializer_list<absl::Status> children) GRPC_MUST_USE_RESULT;

/// Set the grpc_error_ints property to the status
void grpc_status_set_int(absl::Status* status, grpc_error_ints which,
                         intptr_t value);

/// Get the grpc_error_ints property from the status
absl::optional<intptr_t> grpc_status_get_int(
    const absl::Status& status, grpc_error_ints which) GRPC_MUST_USE_RESULT;

/// Set the grpc_error_strs property to the status
void grpc_status_set_str(absl::Status* status, grpc_error_strs which,
                         std::string value);

/// Get the grpc_error_strs property from the status
absl::optional<std::string> grpc_status_get_str(
    const absl::Status& status, grpc_error_strs which) GRPC_MUST_USE_RESULT;

/// Added a child status to status
void grpc_status_add_child(absl::Status* status, absl::Status child);

/// Returns a string representation from status
/// Error status will be like
///   STATUS[:MESSAGE] [{PAYLOADS[, children:[CHILDREN-STATUS-LISTS]]}]
/// e.g.
///   CANCELLATION:SampleMessage {errno:'2021', line:'54', children:[ABORTED]}
std::string grpc_status_to_string(const absl::Status& status)
    GRPC_MUST_USE_RESULT;

/// Log status
void grpc_log_status(const char* what, absl::Status status, const char* file,
                     int line);

// -------------
// Create helper

/// Create an OS error status with given additional information
absl::Status grpc_status_os_create(const char* file, int line, int err,
                                   const char* call_name) GRPC_MUST_USE_RESULT;

#ifdef GPR_WINDOWS
/// Create a WSA error status with given additional information
absl::Status grpc_status_wsa_create(const char* file, int line, int err,
                                    const char* call_name) GRPC_MUST_USE_RESULT;
#endif  // #ifdef GPR_WINDOWS

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_IOMGR_STATUS_UTIL_H
