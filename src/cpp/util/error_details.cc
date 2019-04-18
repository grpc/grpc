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

#include <grpcpp/support/error_details.h>

#include "src/proto/grpc/status/status.pb.h"

namespace grpc_impl {

grpc::Status ExtractErrorDetails(const grpc::Status& from,
                                 ::google::rpc::Status* to) {
  if (to == nullptr) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "");
  }
  if (!to->ParseFromString(from.error_details())) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "");
  }
  return grpc::Status::OK;
}

grpc::Status SetErrorDetails(const ::google::rpc::Status& from,
                             grpc::Status* to) {
  if (to == nullptr) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "");
  }
  grpc::StatusCode code = grpc::StatusCode::UNKNOWN;
  if (from.code() >= grpc::StatusCode::OK &&
      from.code() <= grpc::StatusCode::UNAUTHENTICATED) {
    code = static_cast<grpc::StatusCode>(from.code());
  }
  *to = grpc::Status(code, from.message(), from.SerializeAsString());
  return grpc::Status::OK;
}

}  // namespace grpc_impl
