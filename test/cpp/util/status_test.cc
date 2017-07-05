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

#include <grpc++/support/status.h>

#include <grpc/status.h>
#include <grpc/support/log.h>

// Make sure the existing grpc_status_code match with grpc::Code.
int main(int argc, char** argv) {
  GPR_ASSERT(grpc::StatusCode::OK ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_OK));
  GPR_ASSERT(grpc::StatusCode::CANCELLED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_CANCELLED));
  GPR_ASSERT(grpc::StatusCode::UNKNOWN ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_UNKNOWN));
  GPR_ASSERT(grpc::StatusCode::INVALID_ARGUMENT ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_INVALID_ARGUMENT));
  GPR_ASSERT(grpc::StatusCode::DEADLINE_EXCEEDED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_DEADLINE_EXCEEDED));
  GPR_ASSERT(grpc::StatusCode::NOT_FOUND ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_NOT_FOUND));
  GPR_ASSERT(grpc::StatusCode::ALREADY_EXISTS ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_ALREADY_EXISTS));
  GPR_ASSERT(grpc::StatusCode::PERMISSION_DENIED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_PERMISSION_DENIED));
  GPR_ASSERT(grpc::StatusCode::UNAUTHENTICATED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_UNAUTHENTICATED));
  GPR_ASSERT(grpc::StatusCode::RESOURCE_EXHAUSTED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_RESOURCE_EXHAUSTED));
  GPR_ASSERT(grpc::StatusCode::FAILED_PRECONDITION ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_FAILED_PRECONDITION));
  GPR_ASSERT(grpc::StatusCode::ABORTED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_ABORTED));
  GPR_ASSERT(grpc::StatusCode::OUT_OF_RANGE ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_OUT_OF_RANGE));
  GPR_ASSERT(grpc::StatusCode::UNIMPLEMENTED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_UNIMPLEMENTED));
  GPR_ASSERT(grpc::StatusCode::INTERNAL ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_INTERNAL));
  GPR_ASSERT(grpc::StatusCode::UNAVAILABLE ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_UNAVAILABLE));
  GPR_ASSERT(grpc::StatusCode::DATA_LOSS ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_DATA_LOSS));

  return 0;
}
