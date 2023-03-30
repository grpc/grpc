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

#ifndef GRPC_SRC_CORE_LIB_CONFIG_LOAD_CONFIG_H
#define GRPC_SRC_CORE_LIB_CONFIG_LOAD_CONFIG_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <string>

#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace grpc_core {

std::string LoadConfigFromEnv(absl::string_view environment_variable,
                              const char* default_value);
int32_t LoadConfigFromEnv(absl::string_view environment_variable,
                          int32_t default_value);
bool LoadConfigFromEnv(absl::string_view environment_variable,
                       bool default_value);

// There exists code that pokes values into the absl flags that we have, and can
// manage to smuggle an illegal empty string into an absl::optional<std::string>
// flag.
// To handle this case, we prove a SanitizeValue function here for each flag
// type that will massage things into a safe state.

inline absl::optional<std::string> SanitizeValue(
    absl::optional<std::string> value) {
  if (value.has_value() && value->empty()) return absl::nullopt;
  return value;
}

inline absl::optional<int32_t> SanitizeValue(absl::optional<int32_t> value) {
  return value;
}

inline absl::optional<bool> SanitizeValue(absl::optional<bool> value) {
  return value;
}

template <typename T, typename D>
T LoadConfig(const absl::Flag<absl::optional<T>>& flag,
             absl::string_view environment_variable,
             const absl::optional<T>& override, D default_value) {
  if (override.has_value()) return *override;
  auto from_flag = SanitizeValue(absl::GetFlag(flag));
  if (from_flag.has_value()) return std::move(*from_flag);
  return LoadConfigFromEnv(environment_variable, default_value);
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_CONFIG_LOAD_CONFIG_H
