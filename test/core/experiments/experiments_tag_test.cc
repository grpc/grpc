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

#include <grpc/support/port_platform.h>

#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include "src/core/lib/config/config_vars.h"
#include "test/core/experiments/fixtures/experiments.h"

#ifndef GRPC_EXPERIMENTS_ARE_FINAL

absl::StatusOr<bool> IsExperimentEnabledThroughFlag(
    std::string experiment_name) {
  for (auto experiment :
       absl::StrSplit(grpc_core::ConfigVars::Get().Experiments(), ',',
                      absl::SkipWhitespace())) {
    // Enable unless prefixed with '-' (=> disable).
    bool enable = true;
    if (experiment[0] == '-') {
      enable = false;
      experiment.remove_prefix(1);
    }
    // See if we can find the experiment in the list in this binary.
    if (experiment == experiment_name) {
      return enable;
    }
  }
  return absl::NotFoundError("experiment not found");
}

TEST(ExperimentsTestTagTest, CheckExperimentValuesTest) {
  auto status = IsExperimentEnabledThroughFlag("test_experiment_1");
  if (!status.ok()) {
    return;
  }
#ifdef GRPC_CFSTREAM
  FAIL() << "test_experiment_1 is broken on ios. so this test should not have "
            "executed on RBE."
#elif GPR_WINDOWS
  // Since default on windows is false, when this test is run using the
  // command line vars, we expect that the value of the experiment should be
  // true since this test uses the test_tag. This test should not execute
  // with experiment set to false using the command line vars.
  EXPECT_TRUE(*status);
  EXPECT_TRUE(grpc_core::IsTestExperiment1Enabled());
#else
  // Since default on posix is debug, when this test is run using the
  // command line vars, we expect that the value of the experiment should be
  // false since this test uses the test_tag. This test should not execute
  // with experiment set to true using the command line vars.
  EXPECT_FALSE(*status);
  EXPECT_FALSE(grpc_core::IsTestExperiment1Enabled());
#endif
}

#endif  // GRPC_EXPERIMENTS_ARE_FINAL

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc_core::LoadTestOnlyExperimentsFromMetadata(
      grpc_core::g_test_experiment_metadata, grpc_core::kNumTestExperiments);
  return RUN_ALL_TESTS();
}
