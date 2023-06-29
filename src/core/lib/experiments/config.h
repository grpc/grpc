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

#ifndef GRPC_SRC_CORE_LIB_EXPERIMENTS_CONFIG_H
#define GRPC_SRC_CORE_LIB_EXPERIMENTS_CONFIG_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"

// #define GRPC_EXPERIMENTS_ARE_FINAL

namespace grpc_core {

struct ExperimentMetadata {
  const char* name;
  const char* description;
  const char* additional_constaints;
  bool default_value;
  bool allow_in_fuzzing_config;
};

#ifndef GRPC_EXPERIMENTS_ARE_FINAL
// Return true if experiment \a experiment_id is enabled.
// Experiments are numbered by their order in the g_experiment_metadata array
// declared in experiments.h.
bool IsExperimentEnabled(size_t experiment_id);

// Given a test experiment id, returns true if the test experiment is enabled.
// Test experiments can be loaded using the LoadTestOnlyExperimentsFromMetadata
// method.
bool IsTestExperimentEnabled(size_t experiment_id);

// Reload experiment state from config variables.
// Does not change ForceEnableExperiment state.
// Expects the caller to handle global thread safety - so really only
// appropriate for carefully written tests.
void TestOnlyReloadExperimentsFromConfigVariables();

// Reload experiment state from passed metadata.
// Does not change ForceEnableExperiment state.
// Expects the caller to handle global thread safety - so really only
// appropriate for carefully written tests.
void LoadTestOnlyExperimentsFromMetadata(
    const ExperimentMetadata* experiment_metadata, size_t num_experiments);
#endif

// Print out a list of all experiments that are built into this binary.
void PrintExperimentsList();

// Force an experiment to be on or off.
// Must be called before experiments are configured (the first
// IsExperimentEnabled call).
// If the experiment does not exist, emits a warning but continues execution.
// If this is called twice for the same experiment, both calls must agree.
void ForceEnableExperiment(absl::string_view experiment_name, bool enable);

// Register a function to be called to validate the value an experiment can
// take subject to additional constraints.
// The function will take the ExperimentMetadata as its argument. It will return
// a bool value indicating the actual value the experiment should take.
void RegisterExperimentConstraintsValidator(
    absl::AnyInvocable<bool(struct ExperimentMetadata)> check_constraints_cb);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_EXPERIMENTS_CONFIG_H
