//
//
// Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_TLS_UTILS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_TLS_UTILS_H

#include <grpc/support/port_platform.h>

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace grpc_core {

// Matches \a subject_alternative_name with \a matcher. Returns true if there
// is a match, false otherwise.
bool VerifySubjectAlternativeName(absl::string_view subject_alternative_name,
                                  const std::string& matcher);

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_TLS_UTILS_H
