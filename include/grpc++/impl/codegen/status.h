/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPCXX_IMPL_CODEGEN_STATUS_H
#define GRPCXX_IMPL_CODEGEN_STATUS_H

#include <grpc++/impl/codegen/config.h>
#include <grpc++/impl/codegen/status_code_enum.h>

namespace grpc {

/// Did it work? If it didn't, why?
///
/// See \a grpc::StatusCode for details on the available code and their meaning.
class Status {
 public:
  /// Construct an OK instance.
  Status() : code_(StatusCode::OK) {}

  /// Construct an instance with associated \a code and \a details (also
  // referred to as "error_message").
  Status(StatusCode code, const grpc::string& details)
      : code_(code), details_(details) {}

  // Pre-defined special status objects.
  /// An OK pre-defined instance.
  static const Status& OK;
  /// A CANCELLED pre-defined instance.
  static const Status& CANCELLED;

  /// Return the instance's error code.
  StatusCode error_code() const { return code_; }
  /// Return the instance's error message.
  grpc::string error_message() const { return details_; }

  /// Is the status OK?
  bool ok() const { return code_ == StatusCode::OK; }

 private:
  StatusCode code_;
  grpc::string details_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_STATUS_H
