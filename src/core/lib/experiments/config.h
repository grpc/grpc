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

#ifndef GRPC_CORE_LIB_EXPERIMENTS_CONFIG_H
#define GRPC_CORE_LIB_EXPERIMENTS_CONFIG_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include "absl/strings/string_view.h"

namespace grpc_core {

// Return true if experiment \a experiment_id is enabled.
// Experiments are numbered by their order in the g_experiment_metadata array
// declared in experiments.h.
bool IsExperimentEnabled(size_t experiment_id);

// Print out a list of all experiments that are built into this binary.
void PrintExperimentsList();

// Force an experiment to be on or off.
// Must be called before experiments are configured (the first
// IsExperimentEnabled call).
// If the experiment does not exist, emits a warning but continues execution.
// If this is called twice for the same experiment, both calls must agree.
void ForceEnableExperiment(absl::string_view experiment_name, bool enable);

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_EXPERIMENTS_CONFIG_H
