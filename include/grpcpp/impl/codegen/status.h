/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_STATUS_H
#define GRPCPP_IMPL_CODEGEN_STATUS_H

#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/status_code_enum.h>
#include <grpcpp/export.h>

namespace grpc {

/// Did it work? If it didn't, why?
///
/// See \a grpc::StatusCode for details on the available code and their meaning.
class Status {
 public:
  /// Construct an OK instance.
  GRPCXX_EXPORT Status() : code_(StatusCode::OK) {}

  /// Construct an instance with associated \a code and \a error_message.
  /// It is an error to construct an OK status with non-empty \a error_message.
  GRPCXX_EXPORT Status(StatusCode code, const grpc::string& error_message)
      : code_(code), error_message_(error_message) {}

  /// Construct an instance with \a code,  \a error_message and
  /// \a error_details. It is an error to construct an OK status with non-empty
  /// \a error_message and/or \a error_details.
  GRPCXX_EXPORT Status(StatusCode code, const grpc::string& error_message,
         const grpc::string& error_details)
      : code_(code),
        error_message_(error_message),
        binary_error_details_(error_details) {}

  // Pre-defined special status objects.
  /// An OK pre-defined instance.
  GRPCXX_EXPORT static const Status& OK;
  /// A CANCELLED pre-defined instance.
  GRPCXX_EXPORT static const Status& CANCELLED;

  /// Return the instance's error code.
  GRPCXX_EXPORT StatusCode error_code() const { return code_; }
  /// Return the instance's error message.
  GRPCXX_EXPORT grpc::string error_message() const { return error_message_; }
  /// Return the (binary) error details.
  // Usually it contains a serialized google.rpc.Status proto.
  GRPCXX_EXPORT grpc::string error_details() const { return binary_error_details_; }

  /// Is the status OK?
  GRPCXX_EXPORT bool ok() const { return code_ == StatusCode::OK; }

  // Ignores any errors. This method does nothing except potentially suppress
  // complaints from any tools that are checking that errors are not dropped on
  // the floor.
  GRPCXX_EXPORT void IgnoreError() const {}

 private:
  StatusCode code_;
  grpc::string error_message_;
  grpc::string binary_error_details_;
};

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_STATUS_H
