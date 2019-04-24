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

#ifndef GRPC_CORE_LIB_GPR_GLOBAL_CONFIG_ENV_H
#define GRPC_CORE_LIB_GPR_GLOBAL_CONFIG_ENV_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/global_config_generic.h"
#include "src/core/lib/gprpp/memory.h"

namespace grpc_core {

typedef void (*GlobalConfigEnvErrorFunctionType)(const char* error_message);

/*
 * Set global_config_env_error_function which is called when config system
 * encounters errors such as parsing error. What the default function does
 * is logging error message.
 */
void SetGlobalConfigEnvErrorFunction(GlobalConfigEnvErrorFunctionType func);

// Base class for all classes to access environment variables.
class GlobalConfigEnv {
 protected:
  // `name` should be wrtiable and should be alive after constructor is called.
  explicit GlobalConfigEnv(char* name);

 public:
  // Returns the value of `name` variable.
  UniquePtr<char> GetValue();

  // Sets the value of `name` variable.
  void SetValue(const char* value);

  // Unsets `name` variable.
  void Unset();

 protected:
  char* name_;
};

class GlobalConfigEnvBool : public GlobalConfigEnv {
 public:
  GlobalConfigEnvBool(char* name, bool default_value);

  bool Get();
  void Set(bool value);

 private:
  bool default_value_;
};

class GlobalConfigEnvInt32 : public GlobalConfigEnv {
 public:
  GlobalConfigEnvInt32(char* name, int32_t default_value);

  int32_t Get();
  void Set(int32_t value);

 private:
  int32_t default_value_;
};

class GlobalConfigEnvString : public GlobalConfigEnv {
 public:
  GlobalConfigEnvString(char* name, const char* default_value);

  UniquePtr<char> Get();
  void Set(const char* value);

 private:
  const char* default_value_;
};

}  // namespace grpc_core

// Macros for defining global config instances using environment variables.
// This defines a GlobalConfigEnv*Type* instance with arguments for
// mutable variable name and default value.
// Mutable name (g_env_str_##name) is here for having an array
// for the canonical name without dynamic allocation.
// `help` argument is ignored for this implementation.

#define GPR_GLOBAL_CONFIG_ENV_DEFINE_BOOL(name, default_value, help)     \
  static char g_env_str_##name[] = #name;                                \
  static ::grpc_core::GlobalConfigEnvBool g_env_##name(g_env_str_##name, \
                                                       default_value);   \
  bool gpr_global_config_get_##name() { return g_env_##name.Get(); }     \
  void gpr_global_config_set_##name(bool value) { g_env_##name.Set(value); }

#define GPR_GLOBAL_CONFIG_ENV_DEFINE_INT32(name, default_value, help)     \
  static char g_env_str_##name[] = #name;                                 \
  static ::grpc_core::GlobalConfigEnvInt32 g_env_##name(g_env_str_##name, \
                                                        default_value);   \
  int32_t gpr_global_config_get_##name() { return g_env_##name.Get(); }   \
  void gpr_global_config_set_##name(int32_t value) { g_env_##name.Set(value); }

#define GPR_GLOBAL_CONFIG_ENV_DEFINE_STRING(name, default_value, help)     \
  static char g_env_str_##name[] = #name;                                  \
  static ::grpc_core::GlobalConfigEnvString g_env_##name(g_env_str_##name, \
                                                         default_value);   \
  ::grpc_core::UniquePtr<char> gpr_global_config_get_##name() {            \
    return g_env_##name.Get();                                             \
  }                                                                        \
  void gpr_global_config_set_##name(const char* value) {                   \
    g_env_##name.Set(value);                                               \
  }

#endif /* GRPC_CORE_LIB_GPR_GLOBAL_CONFIG_ENV_H */
