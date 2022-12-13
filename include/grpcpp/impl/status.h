/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPCPP_IMPL_STATUS_H
#define GRPCPP_IMPL_STATUS_H

// IWYU pragma: private, include <grpcpp/support/status.h>

#include <grpc/support/port_platform.h>

#include <grpc/status.h>
#include <grpcpp/support/config.h>

namespace grpc {

enum StatusCode {
  /// Not an error; returned on success.
  OK = 0,

  /// The operation was cancelled (typically by the caller).
  CANCELLED = 1,

  /// Unknown error. An example of where this error may be returned is if a
  /// Status value received from another address space belongs to an error-space
  /// that is not known in this address space. Also errors raised by APIs that
  /// do not return enough error information may be converted to this error.
  UNKNOWN = 2,

  /// Client specified an invalid argument. Note that this differs from
  /// FAILED_PRECONDITION. INVALID_ARGUMENT indicates arguments that are
  /// problematic regardless of the state of the system (e.g., a malformed file
  /// name).
  INVALID_ARGUMENT = 3,

  /// Deadline expired before operation could complete. For operations that
  /// change the state of the system, this error may be returned even if the
  /// operation has completed successfully. For example, a successful response
  /// from a server could have been delayed long enough for the deadline to
  /// expire.
  DEADLINE_EXCEEDED = 4,

  /// Some requested entity (e.g., file or directory) was not found.
  NOT_FOUND = 5,

  /// Some entity that we attempted to create (e.g., file or directory) already
  /// exists.
  ALREADY_EXISTS = 6,

  /// The caller does not have permission to execute the specified operation.
  /// PERMISSION_DENIED must not be used for rejections caused by exhausting
  /// some resource (use RESOURCE_EXHAUSTED instead for those errors).
  /// PERMISSION_DENIED must not be used if the caller can not be identified
  /// (use UNAUTHENTICATED instead for those errors).
  PERMISSION_DENIED = 7,

  /// The request does not have valid authentication credentials for the
  /// operation.
  UNAUTHENTICATED = 16,

  /// Some resource has been exhausted, perhaps a per-user quota, or perhaps the
  /// entire file system is out of space.
  RESOURCE_EXHAUSTED = 8,

  /// Operation was rejected because the system is not in a state required for
  /// the operation's execution. For example, directory to be deleted may be
  /// non-empty, an rmdir operation is applied to a non-directory, etc.
  ///
  /// A litmus test that may help a service implementor in deciding
  /// between FAILED_PRECONDITION, ABORTED, and UNAVAILABLE:
  ///  (a) Use UNAVAILABLE if the client can retry just the failing call.
  ///  (b) Use ABORTED if the client should retry at a higher-level
  ///      (e.g., restarting a read-modify-write sequence).
  ///  (c) Use FAILED_PRECONDITION if the client should not retry until
  ///      the system state has been explicitly fixed. E.g., if an "rmdir"
  ///      fails because the directory is non-empty, FAILED_PRECONDITION
  ///      should be returned since the client should not retry unless
  ///      they have first fixed up the directory by deleting files from it.
  ///  (d) Use FAILED_PRECONDITION if the client performs conditional
  ///      REST Get/Update/Delete on a resource and the resource on the
  ///      server does not match the condition. E.g., conflicting
  ///      read-modify-write on the same resource.
  FAILED_PRECONDITION = 9,

  /// The operation was aborted, typically due to a concurrency issue like
  /// sequencer check failures, transaction aborts, etc.
  ///
  /// See litmus test above for deciding between FAILED_PRECONDITION, ABORTED,
  /// and UNAVAILABLE.
  ABORTED = 10,

  /// Operation was attempted past the valid range. E.g., seeking or reading
  /// past end of file.
  ///
  /// Unlike INVALID_ARGUMENT, this error indicates a problem that may be fixed
  /// if the system state changes. For example, a 32-bit file system will
  /// generate INVALID_ARGUMENT if asked to read at an offset that is not in the
  /// range [0,2^32-1], but it will generate OUT_OF_RANGE if asked to read from
  /// an offset past the current file size.
  ///
  /// There is a fair bit of overlap between FAILED_PRECONDITION and
  /// OUT_OF_RANGE. We recommend using OUT_OF_RANGE (the more specific error)
  /// when it applies so that callers who are iterating through a space can
  /// easily look for an OUT_OF_RANGE error to detect when they are done.
  OUT_OF_RANGE = 11,

  /// Operation is not implemented or not supported/enabled in this service.
  UNIMPLEMENTED = 12,

  /// Internal errors. Means some invariants expected by underlying System has
  /// been broken. If you see one of these errors, Something is very broken.
  INTERNAL = 13,

  /// The service is currently unavailable. This is a most likely a transient
  /// condition and may be corrected by retrying with a backoff. Note that it is
  /// not always safe to retry non-idempotent operations.
  ///
  /// \warning Although data MIGHT not have been transmitted when this
  /// status occurs, there is NOT A GUARANTEE that the server has not seen
  /// anything. So in general it is unsafe to retry on this status code
  /// if the call is non-idempotent.
  ///
  /// See litmus test above for deciding between FAILED_PRECONDITION, ABORTED,
  /// and UNAVAILABLE.
  UNAVAILABLE = 14,

  /// Unrecoverable data loss or corruption.
  DATA_LOSS = 15,

  /// Force users to include a default branch:
  DO_NOT_USE = -1
};

/// Did it work? If it didn't, why?
///
/// See \a grpc::StatusCode for details on the available code and their meaning.
class GRPC_MUST_USE_RESULT_WHEN_USE_STRICT_WARNING Status {
 public:
  /// Construct an OK instance.
  Status() : code_(StatusCode::OK) {
    // Static assertions to make sure that the C++ API value correctly
    // maps to the core surface API value
    static_assert(StatusCode::OK == static_cast<StatusCode>(GRPC_STATUS_OK),
                  "Mismatched status code");
    static_assert(
        StatusCode::CANCELLED == static_cast<StatusCode>(GRPC_STATUS_CANCELLED),
        "Mismatched status code");
    static_assert(
        StatusCode::UNKNOWN == static_cast<StatusCode>(GRPC_STATUS_UNKNOWN),
        "Mismatched status code");
    static_assert(StatusCode::INVALID_ARGUMENT ==
                      static_cast<StatusCode>(GRPC_STATUS_INVALID_ARGUMENT),
                  "Mismatched status code");
    static_assert(StatusCode::DEADLINE_EXCEEDED ==
                      static_cast<StatusCode>(GRPC_STATUS_DEADLINE_EXCEEDED),
                  "Mismatched status code");
    static_assert(
        StatusCode::NOT_FOUND == static_cast<StatusCode>(GRPC_STATUS_NOT_FOUND),
        "Mismatched status code");
    static_assert(StatusCode::ALREADY_EXISTS ==
                      static_cast<StatusCode>(GRPC_STATUS_ALREADY_EXISTS),
                  "Mismatched status code");
    static_assert(StatusCode::PERMISSION_DENIED ==
                      static_cast<StatusCode>(GRPC_STATUS_PERMISSION_DENIED),
                  "Mismatched status code");
    static_assert(StatusCode::UNAUTHENTICATED ==
                      static_cast<StatusCode>(GRPC_STATUS_UNAUTHENTICATED),
                  "Mismatched status code");
    static_assert(StatusCode::RESOURCE_EXHAUSTED ==
                      static_cast<StatusCode>(GRPC_STATUS_RESOURCE_EXHAUSTED),
                  "Mismatched status code");
    static_assert(StatusCode::FAILED_PRECONDITION ==
                      static_cast<StatusCode>(GRPC_STATUS_FAILED_PRECONDITION),
                  "Mismatched status code");
    static_assert(
        StatusCode::ABORTED == static_cast<StatusCode>(GRPC_STATUS_ABORTED),
        "Mismatched status code");
    static_assert(StatusCode::OUT_OF_RANGE ==
                      static_cast<StatusCode>(GRPC_STATUS_OUT_OF_RANGE),
                  "Mismatched status code");
    static_assert(StatusCode::UNIMPLEMENTED ==
                      static_cast<StatusCode>(GRPC_STATUS_UNIMPLEMENTED),
                  "Mismatched status code");
    static_assert(
        StatusCode::INTERNAL == static_cast<StatusCode>(GRPC_STATUS_INTERNAL),
        "Mismatched status code");
    static_assert(StatusCode::UNAVAILABLE ==
                      static_cast<StatusCode>(GRPC_STATUS_UNAVAILABLE),
                  "Mismatched status code");
    static_assert(
        StatusCode::DATA_LOSS == static_cast<StatusCode>(GRPC_STATUS_DATA_LOSS),
        "Mismatched status code");
  }

  /// Construct an instance with associated \a code and \a error_message.
  /// It is an error to construct an OK status with non-empty \a error_message.
  /// Note that \a message is intentionally accepted as a const reference
  /// instead of a value (which results in a copy instead of a move) to allow
  /// for easy transition to absl::Status in the future which accepts an
  /// absl::string_view as a parameter.
  Status(StatusCode code, const std::string& error_message)
      : code_(code), error_message_(error_message) {}

  /// Construct an instance with \a code,  \a error_message and
  /// \a error_details. It is an error to construct an OK status with non-empty
  /// \a error_message and/or \a error_details.
  Status(StatusCode code, const std::string& error_message,
         const std::string& error_details)
      : code_(code),
        error_message_(error_message),
        binary_error_details_(error_details) {}

  // Pre-defined special status objects.
  /// An OK pre-defined instance.
  static const Status& OK;
  /// A CANCELLED pre-defined instance.
  static const Status& CANCELLED;

  /// Return the instance's error code.
  StatusCode error_code() const { return code_; }
  /// Return the instance's error message.
  std::string error_message() const { return error_message_; }
  /// Return the (binary) error details.
  // Usually it contains a serialized google.rpc.Status proto.
  std::string error_details() const { return binary_error_details_; }

  /// Is the status OK?
  bool ok() const { return code_ == StatusCode::OK; }

  // Ignores any errors. This method does nothing except potentially suppress
  // complaints from any tools that are checking that errors are not dropped on
  // the floor.
  void IgnoreError() const {}

 private:
  StatusCode code_;
  std::string error_message_;
  std::string binary_error_details_;
};

}  // namespace grpc

#endif  // GRPCPP_IMPL_STATUS_H
