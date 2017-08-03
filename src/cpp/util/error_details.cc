/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc++/support/error_details.h>

#include "src/proto/grpc/status/status.pb.h"

namespace grpc {

Status ExtractErrorDetails(const Status& from, ::google::rpc::Status* to) {
  if (to == nullptr) {
    return Status(StatusCode::FAILED_PRECONDITION, "");
  }
  if (!to->ParseFromString(from.error_details())) {
    return Status(StatusCode::INVALID_ARGUMENT, "");
  }
  return Status::OK;
}

Status SetErrorDetails(const ::google::rpc::Status& from, Status* to) {
  if (to == nullptr) {
    return Status(StatusCode::FAILED_PRECONDITION, "");
  }
  StatusCode code = StatusCode::UNKNOWN;
  if (from.code() >= StatusCode::OK && from.code() <= StatusCode::DATA_LOSS) {
    code = static_cast<StatusCode>(from.code());
  }
  *to = Status(code, from.message(), from.SerializeAsString());
  return Status::OK;
}

}  // namespace grpc
