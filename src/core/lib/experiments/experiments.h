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

// Automatically generated by tools/codegen/core/gen_experiments.py

#ifndef GRPC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H
#define GRPC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

namespace grpc_core {

bool IsTcpFrameSizeTuningEnabled();

struct ExperimentMetadata {
  const char* name;
  const char* description;
  bool default_value;
  bool (*is_enabled)();
};

constexpr const size_t kNumExperiments = 1;
extern const ExperimentMetadata g_experiment_metadata[kNumExperiments];

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H
