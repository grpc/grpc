// Copyright 2024 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/auto_scaler.h"

#include "absl/random/random.h"
#include "gtest/gtest.h"
#include "src/core/util/tdigest.h"

namespace grpc_core {
namespace chaotic_good {
namespace {

TDigest RandomDigest(double median, double stddev, size_t samples = 1000) {
  absl::BitGen gen;
  TDigest digest(AutoScaler::Metrics::compression());
  for (int i = 0; i < samples; i++) {
    digest.Add(absl::Gaussian<double>(gen, median, stddev));
  }
  return digest;
}

TDigest BimodalRandomDigest(double peak1_bias, double peak1,
                            double stddev_peak1, double peak2,
                            double stddev_peak2, size_t samples = 1000) {
  absl::BitGen gen;
  TDigest digest(AutoScaler::Metrics::compression());
  for (int i = 0; i < samples; i++) {
    if (absl::Bernoulli(gen, peak1_bias)) {
      digest.Add(absl::Gaussian<double>(gen, peak1, stddev_peak1));
    } else {
      digest.Add(absl::Gaussian<double>(gen, peak2, stddev_peak2));
    }
  }
  return digest;
}

TEST(PreReqTest, RandomDigestWorks) {
  EXPECT_NEAR(RandomDigest(100.0, 10.0).Quantile(0.5), 100.0, 3.0);
}

TEST(PreReqTest, BimodalRandomDigestWorks) {
  EXPECT_NEAR(BimodalRandomDigest(1.0, 100.0, 10.0, 200.0, 10.0).Quantile(0.5),
              100.0, 3.0);
  EXPECT_NEAR(BimodalRandomDigest(0.0, 100.0, 10.0, 200.0, 10.0).Quantile(0.5),
              200.0, 3.0);
}

}  // namespace

namespace autoscaler_detail {

TEST(EvaluateOneSidedExperimentTest, ClearlyBetter) {
  auto before = RandomDigest(100.0, 10.0);
  auto after = RandomDigest(50.0, 10.0);
  EXPECT_EQ(EvaluateOneSidedExperiment(before, after),
            ExperimentResult::kSuccess);
}

TEST(EvaluateOneSidedExperimentTest, ClearlyWorse) {
  auto before = RandomDigest(100.0, 10.0);
  auto after = RandomDigest(150.0, 10.0);
  EXPECT_EQ(EvaluateOneSidedExperiment(before, after),
            ExperimentResult::kFailure);
}

TEST(EvaluateOneSidedExperimentTest, TailClearlyWorse) {
  auto before = RandomDigest(100.0, 10.0);
  auto after = BimodalRandomDigest(0.1, 100.0, 10.0, 150, 10.0);
  EXPECT_EQ(EvaluateOneSidedExperiment(before, after),
            ExperimentResult::kFailure);
}

TEST(ChooseWorstTailLatencyTest, WorksForClient) {
  std::vector<Metrics> metrics;
  for (int i = 0; i < 100; i++) {
    if (i == 3) {
      metrics.push_back(Metrics(RandomDigest(150, 10), RandomDigest(100, 10)));
    } else {
      metrics.push_back(Metrics(RandomDigest(100, 10), RandomDigest(100, 10)));
    }
  }
  EXPECT_EQ(ChooseWorstTailLatency(std::move(metrics)), 3);
}

TEST(ChooseWorstTailLatencyTest, WorksForServer) {
  std::vector<Metrics> metrics;
  for (int i = 0; i < 100; i++) {
    if (i == 3) {
      metrics.push_back(Metrics(RandomDigest(100, 10), RandomDigest(150, 10)));
    } else {
      metrics.push_back(Metrics(RandomDigest(100, 10), RandomDigest(100, 10)));
    }
  }
  EXPECT_EQ(ChooseWorstTailLatency(std::move(metrics)), 3);
}

}  // namespace autoscaler_detail
}  // namespace chaotic_good
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
