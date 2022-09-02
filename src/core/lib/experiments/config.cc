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

#include "src/core/lib/experiments/config.h"

#include "absl/strings/str_split.h"

#include <grpc/support/log.h>

#include "src/core/lib/experiments/config.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/global_config.h"
#include "src/core/lib/gprpp/no_destruct.h"

GPR_GLOBAL_CONFIG_DEFINE_STRING(
    grpc_experiments, "",
    "List of grpc experiments to enable (or with a '-' prefix to disable).");

namespace grpc_core {

namespace {
struct Experiments {
  bool enabled[kNumExperiments];
};
}  // namespace

bool IsExperimentEnabled(size_t experiment_id) {
  static const NoDestruct<Experiments> experiments{[] {
    Experiments experiments;
    for (size_t i = 0; i < kNumExperiments; i++) {
      experiments.enabled[i] = g_experiment_metadata[i].default_value;
    }
    auto experiments_str = GPR_GLOBAL_CONFIG_GET(grpc_experiments);
    for (auto experiment :
         absl::StrSplit(absl::string_view(experiments_str.get()), ',')) {
      experiment = absl::StripAsciiWhitespace(experiment);
      bool enable = true;
      if (experiment[0] == '-') {
        enable = false;
        experiment.remove_prefix(1);
      }
      bool found = false;
      for (size_t i = 0; i < kNumExperiments; i++) {
        if (experiment == g_experiment_metadata[i].name) {
          experiments.enabled[i] = enable;
          found = true;
          break;
        }
      }
      if (!found) {
        gpr_log(GPR_ERROR, "Unknown experiment: %s",
                std::string(experiment).c_str());
      }
    }
    return experiments;
  }()};
  return experiments->enabled[experiment_id];
}

}  // namespace grpc_core
