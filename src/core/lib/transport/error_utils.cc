//
//
// Copyright 2016 gRPC authors.
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

#include "src/core/lib/transport/error_utils.h"

#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <stdint.h>

#include <vector>

#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/util/status_helper.h"

using grpc_core::http2::Http2ErrorCode;

static grpc_error_handle recursively_find_error_with_field(
    grpc_error_handle error, grpc_core::StatusIntProperty which) {
  intptr_t unused;
  // If the error itself has a status code, return it.
  if (grpc_error_get_int(error, which, &unused)) {
    return error;
  }
  std::vector<absl::Status> children = grpc_core::StatusGetChildren(error);
  for (const absl::Status& child : children) {
    grpc_error_handle result = recursively_find_error_with_field(child, which);
    if (!result.ok()) return result;
  }
  return absl::OkStatus();
}

namespace {

std::optional<Http2ErrorCode> GetHttp2Error(const absl::Status& status) {
  auto http_error_code = grpc_core::StatusGetInt(
      status, grpc_core::StatusIntProperty::kHttp2Error);
  if (http_error_code.has_value()) {
    return static_cast<Http2ErrorCode>(*http_error_code);
  }
  return std::nullopt;
}

}  // namespace

void grpc_error_get_status(grpc_error_handle error,
                           grpc_core::Timestamp deadline,
                           grpc_status_code* code, std::string* message,
                           Http2ErrorCode* http_error,
                           const char** error_string) {
  auto http_error_code = GetHttp2Error(error);
  if (code != nullptr) {
    // If the top-level status code is UNKNOWN and there is an HTTP2 error
    // code attribute set, convert from that.  Otherwise, use the top-level
    // status code.
    if (error.code() == absl::StatusCode::kUnknown &&
        http_error_code.has_value()) {
      *code = grpc_http2_error_to_grpc_status(*http_error_code, deadline);
    } else {
      *code = static_cast<grpc_status_code>(error.code());
    }
  }
  if (message != nullptr) *message = std::string(error.message());
  if (error_string != nullptr && !error.ok()) {
    *error_string = gpr_strdup(grpc_core::StatusToString(error).c_str());
  }
  if (http_error != nullptr) {
    // If the HTTP2 error code attribute is present, use it.  Otherwise,
    // if the status code is something other than UNKNOWN, convert from
    // that.  Otherwise, set a default based on whether the status
    // code is OK.
    if (http_error_code.has_value()) {
      *http_error = *http_error_code;
    } else if (error.code() != absl::StatusCode::kUnknown) {
      *http_error = grpc_status_to_http2_error(
          static_cast<grpc_status_code>(error.code()));
    } else {
      *http_error = error.ok() ? Http2ErrorCode::kNoError
                               : Http2ErrorCode::kInternalError;
    }
  }
}

absl::Status grpc_error_to_absl_status(grpc_error_handle error) {
  return error;
}

grpc_error_handle absl_status_to_grpc_error(absl::Status status) {
  return status;
}

bool grpc_error_has_clear_grpc_status(grpc_error_handle error) {
  return error.code() != absl::StatusCode::kUnknown;
}
