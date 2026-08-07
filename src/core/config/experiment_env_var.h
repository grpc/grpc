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

#ifndef GRPC_SRC_CORE_CONFIG_EXPERIMENT_ENV_VAR_H
#define GRPC_SRC_CORE_CONFIG_EXPERIMENT_ENV_VAR_H

namespace grpc_core {

// Returns true if the specified experiment env var is enabled.
// If the env var is not set, returns default_value.
bool IsExperimentEnvVarEnabled(const char* name, bool default_value = false);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CONFIG_EXPERIMENT_ENV_VAR_H
