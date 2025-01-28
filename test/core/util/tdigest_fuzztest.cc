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
#include "src/core/util/tdigest.h"

using fuzztest::InRange;
using fuzztest::VectorOf;

namespace grpc_core {

double GetTrueQuantile(const std::vector<double>& samples, double quantile) {
  std::vector<double> s = samples;
  double true_idx = static_cast<double>(s.size()) * quantile - 1;
  double idx_left = std::floor(true_idx);
  if (idx_left < 0.0) return 0.0;
  double idx_right = std::ceil(true_idx);

  std::sort(s.begin(), s.end());
  if (idx_left == idx_right) {
    return s[idx_left];
  }
  return s[idx_left] * (idx_right - true_idx) +
         s[idx_right] * (true_idx - idx_left);
}

void QuantilesMatch(std::vector<double> values, double compression,
                    double quantile) {
  TDigest digest(compression);
  for (auto value : values) {
    digest.Add(value);
  }
  EXPECT_NEAR(digest.Quantile(quantile), GetTrueQuantile(values, quantile),
              1.0);
}
FUZZ_TEST(MyTestSuite, QuantilesMatch)
    .WithDomains(VectorOf(InRange(0.0, 10.0)).WithMinSize(100),
                 InRange(20, 2000), InRange(0, 1));

}  // namespace grpc_core
