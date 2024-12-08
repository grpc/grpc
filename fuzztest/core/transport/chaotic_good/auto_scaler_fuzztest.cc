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

#include "absl/container/flat_hash_map.h"
#include "fuzztest/fuzztest.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chaotic_good/auto_scaler.h"

using fuzztest::ElementOf;
using fuzztest::InRange;
using fuzztest::PairOf;
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

void ResultsDominateInconclusiveness(ExperimentResult a) {
  EXPECT_EQ(MergeExperimentResults(a, ExperimentResult::kInconclusive), a);
}
FUZZ_TEST(AutoScaler, ResultsDominateInconclusiveness)
    .WithDomains(AnyExperimentResult());

void ReverseWorks(Experiment a) {
  EXPECT_NE(a, Reverse(a));
  EXPECT_EQ(a, Reverse(Reverse(a)));
}
FUZZ_TEST(AutoScaler, ReverseWorks).WithDomains(AnyExperiment());

void ChooseWorstTailLatencyChoosesSomething(
    std::vector<std::pair<uint32_t, Metrics>> latencies_vec) {
  absl::flat_hash_map<uint32_t, Metrics> latencies;
  for (auto& latency : latencies_vec) {
    latencies.emplace(latency.first, std::move(latency.second));
  }
  absl::flat_hash_set<uint32_t> keys;
  for (auto& latency : latencies) {
    keys.insert(latency.first);
  }
  auto max = latencies.size();
  ASSERT_GE(max, 1);
  const auto choice = ChooseWorstTailLatency(std::move(latencies));
  EXPECT_EQ(keys.count(choice), 1)
      << "choice=" << choice << " from [" << absl::StrJoin(keys, ",") << "]";
}
FUZZ_TEST(AutoScaler, ChooseWorstTailLatencyChoosesSomething)
    .WithDomains(VectorOf(PairOf(InRange<uint32_t>(1, 1000), LatencyMetrics()))
                     .WithMinSize(1));

void EvaluateQuantileWorks(TDigest before, TDigest after, double quantile,
                           double range) {
  if (quantile - range < 1.0) return;
  if (quantile + range > 1.0) return;
  const double before_lower = before.Quantile(quantile - range);
  const double before_upper = before.Quantile(quantile + range);
  EXPECT_LT(before_lower, before_upper);
  const double after_value = after.Quantile(quantile);
  if (after_value < before_lower) {
    EXPECT_EQ(ExperimentResult::kFailure,
              EvaluateQuantile(before, after, quantile, range));
  }
  if (after_value > before_upper) {
    EXPECT_EQ(ExperimentResult::kSuccess,
              EvaluateQuantile(before, after, quantile, range));
  }
  if (after_value >= before_lower && after_value <= before_upper) {
    EXPECT_EQ(ExperimentResult::kInconclusive,
              EvaluateQuantile(before, after, quantile, range));
  }
}
FUZZ_TEST(AutoScaler, EvaluateQuantileWorks)
    .WithDomains(LatencyTDigest(), LatencyTDigest(), InRange(0.0, 1.0),
                 InRange(1e-6, 0.1));

}  // namespace autoscaler_detail
}  // namespace chaotic_good
}  // namespace grpc_core
