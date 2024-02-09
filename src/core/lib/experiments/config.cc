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

#include <grpc/support/port_platform.h>

#include "src/core/lib/experiments/config.h"

#include <string.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include <grpc/support/log.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/crash.h"  // IWYU pragma: keep
#include "src/core/lib/gprpp/no_destruct.h"

#ifndef GRPC_EXPERIMENTS_ARE_FINAL
namespace grpc_core {

namespace {
struct Experiments {
  bool enabled[kNumExperiments];
};

struct ForcedExperiment {
  bool forced = false;
  bool value;
};
ForcedExperiment g_forced_experiments[kNumExperiments];

std::atomic<bool> g_loaded(false);

absl::AnyInvocable<bool(struct ExperimentMetadata)>* g_check_constraints_cb =
    nullptr;

class TestExperiments {
 public:
  TestExperiments(const ExperimentMetadata* experiment_metadata,
                  size_t num_experiments) {
    enabled_ = new bool[num_experiments];
    for (size_t i = 0; i < num_experiments; i++) {
      if (g_check_constraints_cb != nullptr) {
        enabled_[i] = (*g_check_constraints_cb)(experiment_metadata[i]);
      } else {
        enabled_[i] = experiment_metadata[i].default_value;
      }
    }
    // For each comma-separated experiment in the global config:
    for (auto experiment : absl::StrSplit(ConfigVars::Get().Experiments(), ',',
                                          absl::SkipWhitespace())) {
      // Enable unless prefixed with '-' (=> disable).
      bool enable = !absl::ConsumePrefix(&experiment, "-");
      // See if we can find the experiment in the list in this binary.
      for (size_t i = 0; i < num_experiments; i++) {
        if (experiment == experiment_metadata[i].name) {
          enabled_[i] = enable;
          break;
        }
      }
    }
  }

  // Overloading [] operator to access elements in array style
  bool operator[](int index) { return enabled_[index]; }

  ~TestExperiments() { delete enabled_; }

 private:
  bool* enabled_;
};

TestExperiments* g_test_experiments = nullptr;

GPR_ATTRIBUTE_NOINLINE Experiments LoadExperimentsFromConfigVariableInner() {
  // Set defaults from metadata.
  Experiments experiments;
  for (size_t i = 0; i < kNumExperiments; i++) {
    if (!g_forced_experiments[i].forced) {
      if (g_check_constraints_cb != nullptr) {
        experiments.enabled[i] =
            (*g_check_constraints_cb)(g_experiment_metadata[i]);
      } else {
        experiments.enabled[i] = g_experiment_metadata[i].default_value;
      }
    } else {
      experiments.enabled[i] = g_forced_experiments[i].value;
    }
  }
  // For each comma-separated experiment in the global config:
  for (auto experiment : absl::StrSplit(ConfigVars::Get().Experiments(), ',',
                                        absl::SkipWhitespace())) {
    // Enable unless prefixed with '-' (=> disable).
    bool enable = true;
    if (experiment[0] == '-') {
      enable = false;
      experiment.remove_prefix(1);
    }
    // See if we can find the experiment in the list in this binary.
    bool found = false;
    for (size_t i = 0; i < kNumExperiments; i++) {
      if (experiment == g_experiment_metadata[i].name) {
        experiments.enabled[i] = enable;
        found = true;
        break;
      }
    }
    // If not found log an error, but don't take any other action.
    // Allows us an easy path to disabling experiments.
    if (!found) {
      gpr_log(GPR_ERROR, "Unknown experiment: %s",
              std::string(experiment).c_str());
    }
  }
  for (size_t i = 0; i < kNumExperiments; i++) {
    // If required experiments are not enabled, disable this one too.
    for (size_t j = 0; j < g_experiment_metadata[i].num_required_experiments;
         j++) {
      // Require that we can check dependent requirements with a linear sweep
      // (implies the experiments generator must DAG sort the experiments)
      GPR_ASSERT(g_experiment_metadata[i].required_experiments[j] < i);
      if (!experiments
               .enabled[g_experiment_metadata[i].required_experiments[j]]) {
        experiments.enabled[i] = false;
      }
    }
  }
  return experiments;
}

Experiments LoadExperimentsFromConfigVariable() {
  g_loaded.store(true, std::memory_order_relaxed);
  return LoadExperimentsFromConfigVariableInner();
}

Experiments& ExperimentsSingleton() {
  // One time initialization:
  static NoDestruct<Experiments> experiments{
      LoadExperimentsFromConfigVariable()};
  return *experiments;
}
}  // namespace

void TestOnlyReloadExperimentsFromConfigVariables() {
  ExperimentsSingleton() = LoadExperimentsFromConfigVariable();
  PrintExperimentsList();
}

void LoadTestOnlyExperimentsFromMetadata(
    const ExperimentMetadata* experiment_metadata, size_t num_experiments) {
  g_test_experiments =
      new TestExperiments(experiment_metadata, num_experiments);
}

bool IsExperimentEnabled(size_t experiment_id) {
  return ExperimentsSingleton().enabled[experiment_id];
}

bool IsExperimentEnabledInConfiguration(size_t experiment_id) {
  return LoadExperimentsFromConfigVariableInner().enabled[experiment_id];
}

bool IsTestExperimentEnabled(size_t experiment_id) {
  return (*g_test_experiments)[experiment_id];
}

void PrintExperimentsList() {
  size_t max_experiment_length = 0;
  // Populate visitation order into a std::map so that iteration results in a
  // lexical ordering of experiment names.
  // The lexical ordering makes it nice and easy to find the experiment you're
  // looking for in the output spam that we generate.
  std::map<absl::string_view, size_t> visitation_order;
  for (size_t i = 0; i < kNumExperiments; i++) {
    max_experiment_length =
        std::max(max_experiment_length, strlen(g_experiment_metadata[i].name));
    visitation_order[g_experiment_metadata[i].name] = i;
  }
  for (auto name_index : visitation_order) {
    const size_t i = name_index.second;
    gpr_log(
        GPR_DEBUG, "%s",
        absl::StrCat(
            "gRPC EXPERIMENT ", g_experiment_metadata[i].name,
            std::string(max_experiment_length -
                            strlen(g_experiment_metadata[i].name) + 1,
                        ' '),
            IsExperimentEnabled(i) ? "ON " : "OFF",
            " (default:", g_experiment_metadata[i].default_value ? "ON" : "OFF",
            (g_check_constraints_cb != nullptr
                 ? absl::StrCat(
                       " + ", g_experiment_metadata[i].additional_constaints,
                       " => ",
                       (*g_check_constraints_cb)(g_experiment_metadata[i])
                           ? "ON "
                           : "OFF")
                 : std::string()),
            g_forced_experiments[i].forced
                ? absl::StrCat(" force:",
                               g_forced_experiments[i].value ? "ON" : "OFF")
                : std::string(),
            ")")
            .c_str());
  }
}

void ForceEnableExperiment(absl::string_view experiment, bool enable) {
  GPR_ASSERT(g_loaded.load(std::memory_order_relaxed) == false);
  for (size_t i = 0; i < kNumExperiments; i++) {
    if (g_experiment_metadata[i].name != experiment) continue;
    if (g_forced_experiments[i].forced) {
      GPR_ASSERT(g_forced_experiments[i].value == enable);
    } else {
      g_forced_experiments[i].forced = true;
      g_forced_experiments[i].value = enable;
    }
    return;
  }
  gpr_log(GPR_INFO, "gRPC EXPERIMENT %s not found to force %s",
          std::string(experiment).c_str(), enable ? "enable" : "disable");
}

void RegisterExperimentConstraintsValidator(
    absl::AnyInvocable<bool(struct ExperimentMetadata)> check_constraints_cb) {
  g_check_constraints_cb =
      new absl::AnyInvocable<bool(struct ExperimentMetadata)>(
          std::move(check_constraints_cb));
}

}  // namespace grpc_core
#else
namespace grpc_core {
void PrintExperimentsList() {}
void ForceEnableExperiment(absl::string_view experiment_name, bool) {
  Crash(absl::StrCat("ForceEnableExperiment(\"", experiment_name,
                     "\") called in final build"));
}

void RegisterExperimentConstraintsValidator(
    absl::AnyInvocable<
        bool(struct ExperimentMetadata)> /*check_constraints_cb*/) {}

}  // namespace grpc_core
#endif
