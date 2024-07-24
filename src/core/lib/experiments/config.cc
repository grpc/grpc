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

#include <string.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

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

ForcedExperiment* ForcedExperiments() {
  static NoDestruct<ForcedExperiment> forced_experiments[kNumExperiments];
  return &**forced_experiments;
}

std::atomic<bool>* Loaded() {
  static NoDestruct<std::atomic<bool>> loaded(false);
  return &*loaded;
}

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
    if (!ForcedExperiments()[i].forced) {
      if (g_check_constraints_cb != nullptr) {
        experiments.enabled[i] =
            (*g_check_constraints_cb)(g_experiment_metadata[i]);
      } else {
        experiments.enabled[i] = g_experiment_metadata[i].default_value;
      }
    } else {
      experiments.enabled[i] = ForcedExperiments()[i].value;
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
      CHECK(g_experiment_metadata[i].required_experiments[j] < i);
      if (!experiments
               .enabled[g_experiment_metadata[i].required_experiments[j]]) {
        experiments.enabled[i] = false;
      }
    }
  }
  return experiments;
}

Experiments LoadExperimentsFromConfigVariable() {
  Loaded()->store(true, std::memory_order_relaxed);
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
  std::map<std::string, std::string> experiment_status;
  std::set<std::string> defaulted_on_experiments;
  for (size_t i = 0; i < kNumExperiments; i++) {
    const char* name = g_experiment_metadata[i].name;
    const bool enabled = IsExperimentEnabled(i);
    const bool default_enabled = g_experiment_metadata[i].default_value;
    const bool forced = ForcedExperiments()[i].forced;
    if (!default_enabled && !enabled) continue;
    if (default_enabled && enabled) {
      defaulted_on_experiments.insert(name);
      continue;
    }
    if (enabled) {
      if (g_check_constraints_cb != nullptr &&
          (*g_check_constraints_cb)(g_experiment_metadata[i])) {
        experiment_status[name] = "on:constraints";
        continue;
      }
      if (forced && ForcedExperiments()[i].value) {
        experiment_status[name] = "on:forced";
        continue;
      }
      experiment_status[name] = "on";
    } else {
      if (forced && !ForcedExperiments()[i].value) {
        experiment_status[name] = "off:forced";
        continue;
      }
      experiment_status[name] = "off";
    }
  }
  if (experiment_status.empty()) {
    if (!defaulted_on_experiments.empty()) {
      VLOG(2) << "gRPC experiments enabled: "
              << absl::StrJoin(defaulted_on_experiments, ", ");
    }
  } else {
    if (defaulted_on_experiments.empty()) {
      VLOG(2) << "gRPC experiments: "
              << absl::StrJoin(experiment_status, ", ",
                               absl::PairFormatter(":"));
    } else {
      VLOG(2) << "gRPC experiments: "
              << absl::StrJoin(experiment_status, ", ",
                               absl::PairFormatter(":"))
              << "; default-enabled: "
              << absl::StrJoin(defaulted_on_experiments, ", ");
    }
  }
}

void ForceEnableExperiment(absl::string_view experiment, bool enable) {
  CHECK(Loaded()->load(std::memory_order_relaxed) == false);
  for (size_t i = 0; i < kNumExperiments; i++) {
    if (g_experiment_metadata[i].name != experiment) continue;
    if (ForcedExperiments()[i].forced) {
      CHECK(ForcedExperiments()[i].value == enable);
    } else {
      ForcedExperiments()[i].forced = true;
      ForcedExperiments()[i].value = enable;
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
