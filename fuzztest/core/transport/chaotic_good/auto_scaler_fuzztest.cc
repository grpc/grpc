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

// Test to verify Fuzztest integration

#include <vector>

#include "fuzztest/fuzztest.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chaotic_good/auto_scaler.h"

using fuzztest::ElementOf;
using fuzztest::InRange;
using fuzztest::StructOf;
using fuzztest::VectorOf;

namespace grpc_core {
namespace chaotic_good {
namespace autoscaler_detail {

auto AnyExperimentResult() {
  return ElementOf<ExperimentResult>({ExperimentResult::kSuccess,
                                      ExperimentResult::kFailure,
                                      ExperimentResult::kInconclusive});
}

auto AnyExperiment() {
  return ElementOf<Experiment>({Experiment::kUp, Experiment::kDown});
}

auto LatencyTDigest() {
  return fuzztest::Map(
      [](std::vector<double> values, double compression) {
        TDigest out(compression);
        for (double value : values) {
          out.Add(value);
        }
        return out;
      },
      VectorOf(InRange(1.0, 1e6)).WithMinSize(10), InRange(10, 1000));
}

auto LatencyMetrics() {
  return StructOf<Metrics>(LatencyTDigest(), LatencyTDigest());
}

void MergeExperimentResultsIsSymmetric(ExperimentResult a, ExperimentResult b) {
  EXPECT_EQ(MergeExperimentResults(a, b), MergeExperimentResults(b, a));
}
FUZZ_TEST(AutoScaler, MergeExperimentResultsIsSymmetric)
    .WithDomains(AnyExperimentResult(), AnyExperimentResult());

void ReverseWorks(Experiment a) {
  EXPECT_NE(a, Reverse(a));
  EXPECT_EQ(a, Reverse(Reverse(a)));
}
FUZZ_TEST(AutoScaler, ReverseWorks).WithDomains(AnyExperiment());

void ChooseWorstTailLatencyChoosesSomething(std::vector<Metrics> latencies) {
  auto max = latencies.size();
  ASSERT_GE(max, 1);
  const auto choice = ChooseWorstTailLatency(std::move(latencies));
  EXPECT_GE(choice, 0);
  EXPECT_LT(choice, max);
}
FUZZ_TEST(AutoScaler, ChooseWorstTailLatencyChoosesSomething)
    .WithDomains(VectorOf(LatencyMetrics()).WithMinSize(1));

void EvaluateOneSidedExperimentDoesntBarf(TDigest a, TDigest b) {
  bool median_better = b.Quantile(0.5) < a.Quantile(0.5);
  bool tail_better = b.Quantile(0.75) < a.Quantile(0.75);
  auto result = EvaluateOneSidedExperiment(a, b);
  EXPECT_THAT(result, testing::AnyOf(ExperimentResult::kSuccess,
                                     ExperimentResult::kFailure,
                                     ExperimentResult::kInconclusive));
  if (median_better) EXPECT_NE(result, ExperimentResult::kFailure);
  if (tail_better) EXPECT_NE(result, ExperimentResult::kFailure);
  if (!median_better) EXPECT_NE(result, ExperimentResult::kSuccess);
  if (!tail_better) EXPECT_NE(result, ExperimentResult::kSuccess);
}
FUZZ_TEST(AutoScaler, EvaluateOneSidedExperimentDoesntBarf)
    .WithDomains(LatencyTDigest(), LatencyTDigest());

}  // namespace autoscaler_detail
}  // namespace chaotic_good
}  // namespace grpc_core
