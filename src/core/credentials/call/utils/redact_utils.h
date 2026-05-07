// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CREDENTIALS_CALL_UTILS_REDACT_UTILS_H
#define GRPC_SRC_CORE_CREDENTIALS_CALL_UTILS_REDACT_UTILS_H

#include <string>

#include "absl/strings/string_view.h"

namespace grpc_core {

// Redacts sensitive fields from a JSON string.
// Currently redacts "access_token", "refresh_token", and "client_secret".
std::string RedactSensitiveJsonFields(absl::string_view json_string);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_UTILS_REDACT_UTILS_H
