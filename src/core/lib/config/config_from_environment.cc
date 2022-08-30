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

#include "src/core/lib/config/config_from_environment.h"

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/types/optional.h"

#include <grpc/support/alloc.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"

namespace grpc_core {

namespace {
std::string EnvironmentVarFromVarName(const char* var_name) {
  return absl::AsciiStrToUpper(var_name);
}

absl::optional<std::string> LoadEnv(const char* var_name) {
  char* value = gpr_getenv(EnvironmentVarFromVarName(var_name).c_str());
  if (value == nullptr) return absl::nullopt;
  std::string str = value;
  gpr_free(value);
  return str;
}
}  // namespace

std::string LoadStringFromEnv(const char* var_name, const char* default_value) {
  return LoadEnv(var_name).value_or(default_value);
}

int32_t LoadIntFromEnv(const char* var_name, int32_t default_value) {
  auto env = LoadEnv(var_name);
  if (env.has_value()) {
    int32_t out;
    if (absl::SimpleAtoi(*env, &out)) return out;
    fprintf(stderr, "Error reading int from %s: '%s' is not a number",
            EnvironmentVarFromVarName(var_name).c_str(), env->c_str());
  }
  return default_value;
}

bool LoadBoolFromEnv(const char* var_name, bool default_value) {
  auto env = LoadEnv(var_name);
  if (env.has_value()) {
    bool out;
    if (gpr_parse_bool_value(env->c_str(), &out)) return out;
    fprintf(stderr, "Error reading int from %s: '%s' is not a number",
            EnvironmentVarFromVarName(var_name).c_str(), env->c_str());
  }
  return default_value;
}

}  // namespace grpc_core
