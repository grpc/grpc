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

#include <set>
#include <string>

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/experiments/config.h"
#include "src/core/lib/experiments/experiments.h"

namespace grpc_core {

std::string ValidateExperimentsStringForFuzzing(absl::string_view input) {
  std::set<absl::string_view> experiments;
  for (absl::string_view experiment : absl::StrSplit(input, ',')) {
    for (size_t i = 0; i < kNumExperiments; i++) {
      const auto& metadata = g_experiment_metadata[i];
      if (metadata.name == experiment && metadata.allow_in_fuzzing_config) {
        experiments.insert(experiment);
      }
    }
  }
  return absl::StrJoin(experiments, ",");
}

}  // namespace grpc_core
