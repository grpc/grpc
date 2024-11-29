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

#include <string>
#include <utility>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/util/kolmogorov_smirnov.h"

using fuzztest::VectorOf;
using fuzztest::InRange;

namespace grpc_core {
namespace {

void TestThresholdSensitivityAlpha(double alpha, double a_count, double b_count, double delta) {
  EXPECT_GT(
    KolmogorovSmirnovThreshold(alpha, a_count, b_count),
    KolmogorovSmirnovThreshold(alpha + delta, a_count, b_count)
  );
}
FUZZ_TEST(KolmogorovSmirnov, TestThresholdSensitivityAlpha)
  .WithDomains(InRange(0.001, 0.2), InRange(1.0, 100000.0), InRange(1.0, 100000.0), InRange(0.001, 0.1));

void TestThresholdSensitivityReversedCount(double alpha, double a_count, double b_count) {
  EXPECT_NEAR(
    KolmogorovSmirnovThreshold(alpha, a_count, b_count),
    KolmogorovSmirnovThreshold(alpha, b_count, a_count),
    0.00001
  );
}
FUZZ_TEST(KolmogorovSmirnov, TestThresholdSensitivityReversedCount)
  .WithDomains(InRange(0.001, 0.2), InRange(1.0, 100000.0), InRange(1.0, 100000.0));

void TestThresholdSensitivityCount(double alpha, double a_count, double b_count, double delta) {
  EXPECT_LT(
    KolmogorovSmirnovThreshold(alpha, a_count, b_count),
    KolmogorovSmirnovThreshold(alpha, a_count + delta, b_count)
  );
}
FUZZ_TEST(KolmogorovSmirnov, TestThresholdSensitivityCount)
  .WithDomains(InRange(0.001, 0.2), InRange(1.0, 100000.0), InRange(1.0, 100000.0), InRange(1.0, 1000.0));

double ExactStatistic(std::vector<double>& a, std::vector<double>& b) {
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  double max_diff = 0.0;
  for (size_t i=0, j=0; i<a.size() && j<b.size();) {
    double d1 = static_cast<double>(i) / a.size();
    double d2 = static_cast<double>(j) / b.size();
    double diff = std::abs(d1 - d2);
    if (diff > max_diff) {
      max_diff = diff;
    }
    if (a[i] <= b[j]) {
      ++i;
    } else {
      ++j;
    }
  }
  return max_diff;
}

void TestStatistic(std::vector<double> a, std::vector<double> b, double a_compression, double b_compression, uint32_t num_samples) {
  TDigest a_digest(a_compression);
  for (double x : a) {
    a_digest.Add(x);
  }
  TDigest b_digest(b_compression);
  for (double x : b) {
    b_digest.Add(x);
  }
  EXPECT_NEAR(
    KolmogorovSmirnovStatistic(a_digest, b_digest, num_samples),
    ExactStatistic(a, b),
    0.5
  );
}
FUZZ_TEST(KolmogorovSmirnov, TestStatistic)
  .WithDomains(VectorOf(InRange(0.0, 1000.0)).WithMinSize(100), VectorOf(InRange(0.0, 1000.0)).WithMinSize(100), InRange(50.0, 1000.0), InRange(50.0, 1000.0), InRange(10, 100));

}
}
