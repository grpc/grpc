/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

  /// Construct an instance with associated \a code and \a error_message
  Status(StatusCode code, const grpc::string& error_message)
      : code_(code), error_message_(error_message) {}

  /// Construct an instance with \a code,  \a error_message and \a error_details
  Status(StatusCode code, const grpc::string& error_message,
         const grpc::string& error_details)
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
  grpc::string error_message() const { return error_message_; }
  /// Return the (binary) error details.
  // Usually it contains a serialized google.rpc.Status proto.
  grpc::string error_details() const { return binary_error_details_; }

  /// Is the status OK?
  bool ok() const { return code_ == StatusCode::OK; }

 private:
  StatusCode code_;
  grpc::string error_message_;
  grpc::string binary_error_details_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_STATUS_H
