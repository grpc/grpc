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

#ifndef GRPC_CORE_LIB_CONFIG_CONFIG_FROM_ENVIRONMENT_H
#define GRPC_CORE_LIB_CONFIG_CONFIG_FROM_ENVIRONMENT_H

#include <string>

namespace grpc_core {

std::string LoadStringFromEnv(const char* var_name, const char* default_value);
int32_t LoadIntFromEnv(const char* var_name, int32_t default_value);
bool LoadBoolFromEnv(const char* var_name, bool default_value);

}  // namespace grpc_core

#define GRPC_CONFIG_DEFINE_STRING(name, description, default_value)
#define GRPC_CONFIG_DEFINE_INT(name, description, default_value)
#define GRPC_CONFIG_DEFINE_BOOL(name, description, default_value)

#define GRPC_CONFIG_LOAD_STRING(name, description, default_value) \
  ::grpc_core::LoadStringFromEnv(#name, default_value)
#define GRPC_CONFIG_LOAD_INT(name, description, default_value) \
  ::grpc_core::LoadIntFromEnv(#name, default_value)
#define GRPC_CONFIG_LOAD_BOOL(name, description, default_value) \
  ::grpc_core::LoadBoolFromEnv(#name, default_value)

#endif
