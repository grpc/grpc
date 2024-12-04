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

#ifndef GRPCPP_IMPL_STATUS_H
#define GRPCPP_IMPL_STATUS_H

// IWYU pragma: private, include <grpcpp/support/status.h>

#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/support/config.h>
#include <grpcpp/support/status_code_enum.h>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"

namespace grpc {

/// Did it work? If it didn't, why?
///
/// See \a grpc::StatusCode for details on the available code and their meaning.
class GRPC_MUST_USE_RESULT_WHEN_USE_STRICT_WARNING GRPCXX_DLL Status {
 public:
  /// Construct an OK instance.
  Status() = default;

  /// Construct an instance with associated \a code and \a error_message.
  /// It is an error to construct an OK status with non-empty \a error_message.
  /// Note that \a message is intentionally accepted as a const reference
  /// instead of a value (which results in a copy instead of a move) to allow
  /// for easy transition to absl::Status in the future which accepts an
  /// absl::string_view as a parameter.
  // absl::Status {
  Status(StatusCode code, absl::string_view msg)
      : status_(static_cast<absl::StatusCode>(code), msg) {}
  // absl::Status }

  /// Construct an instance with \a code,  \a error_message and
  /// \a error_details. It is an error to construct an OK status with non-empty
  /// \a error_message and/or \a error_details.
  Status(StatusCode code, absl::string_view msg, absl::string_view details)
      : status_(static_cast<absl::StatusCode>(code), msg) {
    if (!details.empty()) {
      status_.SetPayload("_d", absl::Cord(details));
    }
  }

  // Pre-defined special status objects.
  /// An OK pre-defined instance.
  static const Status& OK;
  /// A CANCELLED pre-defined instance.
  static const Status& CANCELLED;

  /// Return the instance's error code.
  StatusCode error_code() const {
    return static_cast<StatusCode>(status_.code());
  }
  /// Return the instance's error message.
  std::string error_message() const { return std::string(status_.message()); }
  /// Return the (binary) error details.
  // Usually it contains a serialized google.rpc.Status proto.
  std::string error_details() const {
    absl::optional<absl::Cord> p = status_.GetPayload("_d");
    if (p.has_value()) {
      return std::string(p.value());
    }
    return "";
  }

  /// Is the status OK?
  bool ok() const { return status_.ok(); }

  // Ignores any errors. This method does nothing except potentially suppress
  // complaints from any tools that are checking that errors are not dropped on
  // the floor.
  void IgnoreError() const {}

  // absl::Status {
  StatusCode code() const { return static_cast<StatusCode>(status_.code()); }
  int raw_code() const { return status_.raw_code(); }
  absl::string_view message() const { return status_.message(); }
  // absl::Status }

 private:
  absl::Status status_;
};

// absl::Status {
inline Status OkStatus() { return Status(); }
// absl::Status }

}  // namespace grpc

#endif  // GRPCPP_IMPL_STATUS_H
