// Copyright 2023 gRPC authors.
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

// Auto generated by tools/codegen/core/gen_experiments.py

#include <grpc/support/port_platform.h>

#include "test/core/experiments/fixtures/experiments.h"

#ifndef GRPC_EXPERIMENTS_ARE_FINAL
namespace {
const char* const description_test_experiment_1 = "Test Experiment 1";
const char* const additional_constraints_test_experiment_1 = "{}";
const char* const description_test_experiment_2 = "Test Experiment 2";
const char* const additional_constraints_test_experiment_2 = "{}";
const char* const description_test_experiment_3 = "Test Experiment 3";
const char* const additional_constraints_test_experiment_3 = "{}";
const char* const description_test_experiment_4 = "Test Experiment 4";
const char* const additional_constraints_test_experiment_4 = "{}";
#ifdef NDEBUG
const bool kDefaultForDebugOnly = false;
#else
const bool kDefaultForDebugOnly = true;
#endif
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_test_experiment_metadata[] = {
    {"test_experiment_1", description_test_experiment_1,
     additional_constraints_test_experiment_1, false, true},
    {"test_experiment_2", description_test_experiment_2,
     additional_constraints_test_experiment_2, false, true},
    {"test_experiment_3", description_test_experiment_3,
     additional_constraints_test_experiment_3, kDefaultForDebugOnly, true},
    {"test_experiment_4", description_test_experiment_4,
     additional_constraints_test_experiment_4, true, true},
};

}  // namespace grpc_core
#endif
