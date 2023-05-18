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

#include <stdio.h>

#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include "src/core/lib/experiments/config.h"
#include "src/core/lib/experiments/experiments.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const auto all_tests = grpc_core::CoreEnd2endTestRegistry::Get().AllTests();
  // We want to produce a set of test cases that exercise all known tests under
  // all known configurations... and we want to include all known experiments in
  // that set.
  // Beyond that, we sort of don't care and expect the fuzzer to do its job.
  std::set<std::pair<std::string, std::string>> suite_and_test_pairs;
  std::set<std::string> configs;
  std::queue<std::string> experiments;
  for (size_t i = 0; i < grpc_core::kNumExperiments; i++) {
    experiments.push(grpc_core::g_experiment_metadata[i].name);
  }
  int file_num = 0;
  for (const auto& test : all_tests) {
    if (test.config->feature_mask & FEATURE_MASK_DO_NOT_FUZZ) continue;
    const bool added_suite =
        suite_and_test_pairs.emplace(test.suite, test.name).second;
    const bool added_config = configs.emplace(test.config->name).second;
    if (!added_suite && !added_config) continue;
    auto text =
        absl::StrCat("suite: \"", test.suite, "\"\n", "test: \"", test.name,
                     "\"\n", "config: \"", test.config->name, "\"\n");
    if (!experiments.empty()) {
      absl::StrAppend(&text, "config_vars {\n  experiments: \"",
                      experiments.front(), "\"\n}\n");
      experiments.pop();
    }
    // We use an index for the filename to keep the path short for Windows
    auto file = absl::StrCat("test/core/end2end/end2end_test_corpus/seed_",
                             file_num, ".textproto");
    ++file_num;
    fprintf(stderr, "WRITE: %s\n", file.c_str());
    FILE* f = fopen(file.c_str(), "w");
    if (!f) return 1;
    auto cleanup = absl::MakeCleanup([f]() { fclose(f); });
    fwrite(text.c_str(), 1, text.size(), f);
  }
}
