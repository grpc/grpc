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

#include "test/core/test_util/fuzz_config_vars_helpers.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <vector>

#include "absl/strings/str_join.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/experiments/experiments.h"

namespace grpc_core {

std::vector<std::string> ExperimentConfigChoices() {
  std::vector<std::string> choices;
  for (size_t i = 0; i < kNumExperiments; i++) {
    if (!g_experiment_metadata[i].allow_in_fuzzing_config) continue;
    choices.push_back(g_experiment_metadata[i].name);
    choices.push_back(absl::StrCat("-", g_experiment_metadata[i].name));
  }
  return choices;
}

std::vector<std::string> TracerConfigChoices() {
  std::vector<std::string> choices;
  for (const auto& [name, _] : GetAllTraceFlags()) {
    choices.push_back(name);
    choices.push_back(absl::StrCat("-", name));
  }
  return choices;
}

}  // namespace grpc_core
