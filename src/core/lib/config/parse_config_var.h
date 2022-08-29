// Copyright 2022 gRPC authors.
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

#ifndef GRPC_CORE_LIB_CONFIG_PARSE_CONFIG_VAR_H
#define GRPC_CORE_LIB_CONFIG_PARSE_CONFIG_VAR_H

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace grpc_core {

int32_t ParseConfigVar(absl::optional<std::string> value,
                       int32_t default_value);
std::string ParseConfigVar(absl::optional<std::string> value,
                           absl::string_view default_value);
bool ParseConfigVar(absl::optional<std::string> value, bool default_value);

}  // namespace grpc_core

#endif
