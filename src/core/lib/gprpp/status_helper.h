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

#include <stdint.h>

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"

#include "src/core/lib/gprpp/debug_location.h"

extern "C" {
struct google_rpc_Status;
struct upb_Arena;
}

#define RETURN_IF_ERROR(expr)           \
  do {                                  \
    const absl::Status status = (expr); \
    if (!status.ok()) return status;    \
  } while (0)

namespace grpc_core {

/// This enum should have the same value of grpc_error_ints
// TODO(veblush): Use camel-case names once migration to absl::Status is done.
enum class StatusIntProperty {
  /// 'errno' from the operating system
  kErrorNo,
  /// __LINE__ from the call site creating the error
  kFileLine,
  /// stream identifier: for errors that are associated with an individual
  /// wire stream
  kStreamId,
  /// grpc status code representing this error
  // TODO(veblush): Remove this after grpc_error is replaced with absl::Status
  kRpcStatus,
  /// offset into some binary blob (usually represented by
  /// RAW_BYTES) where the error occurred
  kOffset,
  /// context sensitive index associated with the error
  kIndex,
  /// context sensitive size associated with the error
  kSize,
  /// http2 error code associated with the error (see the HTTP2 RFC)
  kHttp2Error,
  /// TSI status code associated with the error
  kTsiCode,
  /// WSAGetLastError() reported when this error occurred
  kWsaError,
  /// File descriptor associated with this error
  kFd,
  /// HTTP status (i.e. 404)
  kHttpStatus,
  /// chttp2: did the error occur while a write was in progress
  kOccurredDuringWrite,
  /// channel connectivity state associated with the error
  ChannelConnectivityState,
  /// LB policy drop
  kLbPolicyDrop,
};

/// This enum should have the same value of grpc_error_strs
// TODO(veblush): Use camel-case names once migration to absl::Status is done.
enum class StatusStrProperty {
  /// top-level textual description of this error
  kDescription,
  /// source file in which this error occurred
  kFile,
  /// operating system description of this error
  kOsError,
  /// syscall that generated this error
  kSyscall,
  /// peer that we were trying to communicate when this error occurred
  kTargetAddress,
  /// grpc status message associated with this error
  kGrpcMessage,
  /// hex dump (or similar) with the data that generated this error
  kRawBytes,
  /// tsi error string associated with this error
  kTsiError,
  /// filename that we were trying to read/write when this error occurred
  kFilename,
  /// key associated with the error
  kKey,
  /// value associated with the error
  kValue,
};

/// This enum should have the same value of grpc_error_times
enum class StatusTimeProperty {
  /// timestamp of error creation
  kCreated,
};

/// Creates a status with given additional information
absl::Status StatusCreate(
    absl::StatusCode code, absl::string_view msg, const DebugLocation& location,
    std::vector<absl::Status> children) GRPC_MUST_USE_RESULT;

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

/// Sets the time property to the status
void StatusSetTime(absl::Status* status, StatusTimeProperty key,
                   absl::Time time);

/// Gets the time property from the status
absl::optional<absl::Time> StatusGetTime(
    const absl::Status& status, StatusTimeProperty key) GRPC_MUST_USE_RESULT;

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
google_rpc_Status* StatusToProto(const absl::Status& status,
                                 upb_Arena* arena) GRPC_MUST_USE_RESULT;

/// Builds a status from a upb message, google_rpc_Status
/// This is for internal implementation & test only
absl::Status StatusFromProto(google_rpc_Status* msg) GRPC_MUST_USE_RESULT;

/// Returns ptr that is allocated in the heap memory and the given status is
/// copied into. This ptr can be used to get Status later and should be
/// freed by StatusFreeHeapPtr. This can be 0 in case of OkStatus.
uintptr_t StatusAllocHeapPtr(absl::Status s);

/// Frees the allocated status at heap ptr.
void StatusFreeHeapPtr(uintptr_t ptr);

/// Get the status from a heap ptr.
absl::Status StatusGetFromHeapPtr(uintptr_t ptr);

/// Move the status from a heap ptr. (GetFrom & FreeHeap)
absl::Status StatusMoveFromHeapPtr(uintptr_t ptr);

}  // namespace internal

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_STATUS_HELPER_H
