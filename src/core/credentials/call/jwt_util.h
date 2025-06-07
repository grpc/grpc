//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CREDENTIALS_CALL_JWT_UTIL_H
#define GRPC_SRC_CORE_CREDENTIALS_CALL_JWT_UTIL_H

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/util/time.h"

namespace grpc_core {

// Extract the expiration time from a JWT token based on
// https://datatracker.ietf.org/doc/html/rfc7519.
absl::StatusOr<Timestamp> GetJwtExpirationTime(absl::string_view jwt);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_JWT_UTIL_H
