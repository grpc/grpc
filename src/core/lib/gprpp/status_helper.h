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

/// This enum should have the same value of grpc_error_ints
// TODO(veblush): Use camel-case names once migration to absl::Status is done.
enum class StatusIntProperty {
  /// 'errno' from the operating system
  ERRNO,
  /// __LINE__ from the call site creating the error
  FILE_LINE,
  /// stream identifier: for errors that are associated with an individual
  /// wire stream
  STREAM_ID,
  /// grpc status code representing this error
  // TODO(veblush): Remove this after grpc_error is replaced with absl::Status
  GRPC_STATUS,
  /// offset into some binary blob (usually represented by
  /// RAW_BYTES) where the error occurred
  OFFSET,
  /// context sensitive index associated with the error
  INDEX,
  /// context sensitive size associated with the error
  SIZE,
  /// http2 error code associated with the error (see the HTTP2 RFC)
  HTTP2_ERROR,
  /// TSI status code associated with the error
  TSI_CODE,
  /// WSAGetLastError() reported when this error occurred
  WSA_ERROR,
  /// File descriptor associated with this error
  FD,
  /// HTTP status (i.e. 404)
  HTTP_STATUS,
  /// chttp2: did the error occur while a write was in progress
  OCCURRED_DURING_WRITE,
  /// channel connectivity state associated with the error
  CHANNEL_CONNECTIVITY_STATE,
  /// LB policy drop
  LB_POLICY_DROP,
};

/// This enum should have the same value of grpc_error_strs
// TODO(veblush): Use camel-case names once migration to absl::Status is done.
enum class StatusStrProperty {
  /// top-level textual description of this error
  DESCRIPTION,
  /// source file in which this error occurred
  FILE,
  /// operating system description of this error
  OS_ERROR,
  /// syscall that generated this error
  SYSCALL,
  /// peer that we were trying to communicate when this error occurred
  TARGET_ADDRESS,
  /// grpc status message associated with this error
  GRPC_MESSAGE,
  /// hex dump (or similar) with the data that generated this error
  RAW_BYTES,
  /// tsi error string associated with this error
  TSI_ERROR,
  /// filename that we were trying to read/write when this error occurred
  FILENAME,
  /// key associated with the error
  KEY,
  /// value associated with the error
  VALUE,
  /// time string to create the error
  CREATED_TIME,
};

/// Creates a status with given additional information
absl::Status StatusCreate(
    absl::StatusCode code, absl::string_view msg, const DebugLocation& location,
    std::initializer_list<absl::Status> children) GRPC_MUST_USE_RESULT;

/// Sets the int property to the status
void StatusSetInt(absl::Status* status, StatusIntProperty key, intptr_t value);

/// Gets the int property from the status
absl::optional<intptr_t> StatusGetInt(
    const absl::Status& status, StatusIntProperty key) GRPC_MUST_USE_RESULT;

/// Sets the str property to the status
void StatusSetStr(absl::Status* status, StatusStrProperty key,
                  absl::string_view value);

/// Gets the str property from the status
absl::optional<std::string> StatusGetStr(
    const absl::Status& status, StatusStrProperty key) GRPC_MUST_USE_RESULT;

/// Adds a child status to status
void StatusAddChild(absl::Status* status, absl::Status child);

/// Returns all children status from a status
std::vector<absl::Status> StatusGetChildren(absl::Status status)
    GRPC_MUST_USE_RESULT;

/// Returns a string representation from status
/// Error status will be like
///   STATUS[:MESSAGE] [{PAYLOADS[, children:[CHILDREN-STATUS-LISTS]]}]
/// e.g.
///   CANCELLATION:SampleMessage {errno:'2021', line:'54', children:[ABORTED]}
std::string StatusToString(const absl::Status& status) GRPC_MUST_USE_RESULT;

namespace internal {

/// Builds a upb message, google_rpc_Status from a status
/// This is for internal implementation & test only
google_rpc_Status* StatusToProto(absl::Status status,
                                 upb_arena* arena) GRPC_MUST_USE_RESULT;

/// Build a status from a upb message, google_rpc_Status
/// This is for internal implementation & test only
absl::Status StatusFromProto(google_rpc_Status* msg) GRPC_MUST_USE_RESULT;

}  // namespace internal

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_STATUS_HELPER_H
