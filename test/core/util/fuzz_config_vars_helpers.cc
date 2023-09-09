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

#include <stddef.h>

#include <string>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/experiments/config.h"
#include "src/core/lib/experiments/experiments.h"

namespace grpc_core {

std::string ValidateExperimentsStringForFuzzing(uint64_t input) {
  std::vector<std::string> experiments;
  for (size_t i = 0; i < std::min<size_t>(kNumExperiments, 64); i++) {
    const auto& metadata = g_experiment_metadata[i];
    if ((input & (1ull << i)) && metadata.allow_in_fuzzing_config) {
      experiments.push_back(metadata.name);
    }
  }
  return absl::StrJoin(experiments, ",");
}

}  // namespace grpc_core
