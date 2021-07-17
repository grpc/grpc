/*
 *
 * Copyright 2019 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/global_config_env.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"

#include <ctype.h>
#include <string.h>

#include <string>

#include "absl/strings/str_format.h"

namespace grpc_core {

namespace {

void DefaultGlobalConfigEnvErrorFunction(const char* error_message) {
  gpr_log(GPR_ERROR, "%s", error_message);
}

GlobalConfigEnvErrorFunctionType g_global_config_env_error_func =
    DefaultGlobalConfigEnvErrorFunction;

void LogParsingError(const char* name, const char* value) {
  std::string error_message = absl::StrFormat(
      "Illegal value '%s' specified for environment variable '%s'", value,
      name);
  (*g_global_config_env_error_func)(error_message.c_str());
}

}  // namespace

void SetGlobalConfigEnvErrorFunction(GlobalConfigEnvErrorFunctionType func) {
  g_global_config_env_error_func = func;
}

grpc_core::UniquePtr<char> GlobalConfigEnv::GetValue() {
  return grpc_core::UniquePtr<char>(gpr_getenv(GetName()));
}

void GlobalConfigEnv::SetValue(const char* value) {
  gpr_setenv(GetName(), value);
}

void GlobalConfigEnv::Unset() { gpr_unsetenv(GetName()); }

char* GlobalConfigEnv::GetName() {
  // This makes sure that name_ is in a canonical form having uppercase
  // letters. This is okay to be called serveral times.
  for (char* c = name_; *c != 0; ++c) {
    *c = toupper(*c);
  }
  return name_;
}
static_assert(std::is_trivially_destructible<GlobalConfigEnvBool>::value,
              "GlobalConfigEnvBool needs to be trivially destructible.");

bool GlobalConfigEnvBool::Get() {
  grpc_core::UniquePtr<char> str = GetValue();
  if (str == nullptr) {
    return default_value_;
  }
  // parsing given value string.
  bool result = false;
  if (!gpr_parse_bool_value(str.get(), &result)) {
    LogParsingError(GetName(), str.get());
    result = default_value_;
  }
  return result;
}

void GlobalConfigEnvBool::Set(bool value) {
  SetValue(value ? "true" : "false");
}

static_assert(std::is_trivially_destructible<GlobalConfigEnvInt32>::value,
              "GlobalConfigEnvInt32 needs to be trivially destructible.");

int32_t GlobalConfigEnvInt32::Get() {
  grpc_core::UniquePtr<char> str = GetValue();
  if (str == nullptr) {
    return default_value_;
  }
  // parsing given value string.
  char* end = str.get();
  long result = strtol(str.get(), &end, 10);
  if (*end != 0) {
    LogParsingError(GetName(), str.get());
    result = default_value_;
  }
  return static_cast<int32_t>(result);
}

void GlobalConfigEnvInt32::Set(int32_t value) {
  char buffer[GPR_LTOA_MIN_BUFSIZE];
  gpr_ltoa(value, buffer);
  SetValue(buffer);
}

static_assert(std::is_trivially_destructible<GlobalConfigEnvString>::value,
              "GlobalConfigEnvString needs to be trivially destructible.");

grpc_core::UniquePtr<char> GlobalConfigEnvString::Get() {
  grpc_core::UniquePtr<char> str = GetValue();
  if (str == nullptr) {
    return grpc_core::UniquePtr<char>(gpr_strdup(default_value_));
  }
  return str;
}

void GlobalConfigEnvString::Set(const char* value) { SetValue(value); }

}  // namespace grpc_core
