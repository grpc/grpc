// Copyright 2024 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/metadata.h"

#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

ServerMetadataHandle ServerMetadataFromStatus(const absl::Status& status,
                                              Arena* arena) {
  auto hdl = arena->MakePooled<ServerMetadata>(arena);
  grpc_status_code code;
  std::string message;
  grpc_error_get_status(status, Timestamp::InfFuture(), &code, &message,
                        nullptr, nullptr);
  hdl->Set(GrpcStatusMetadata(), code);
  if (!status.ok()) {
    hdl->Set(GrpcMessageMetadata(), Slice::FromCopiedString(message));
  }
  return hdl;
}

}  // namespace grpc_core
