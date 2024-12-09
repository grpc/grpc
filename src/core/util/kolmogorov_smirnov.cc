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

#include "src/core/util/kolmogorov_smirnov.h"

namespace grpc_core {

bool KolmogorovSmirnovTest(TDigest& a, TDigest& b, double alpha,
                           uint32_t num_samples) {
  return KolmogorovSmirnovStatistic(a, b, num_samples) >
         KolmogorovSmirnovThreshold(alpha, a.Count(), b.Count());
}

double KolmogorovSmirnovStatistic(TDigest& a, TDigest& b,
                                  uint32_t num_samples) {
  const double min_value = std::min(a.Min(), b.Min());
  const double max_value = std::max(a.Max(), b.Max());
  // We don't step to max_value because we know the CDF is 1 there for a & b
  // so we use our samples for the parts of the curve where the CDF actually
  // varies
  const double step = (max_value - min_value) / (num_samples + 1);
  double max_diff = 0;
  for (size_t i = 0; i < num_samples; ++i) {
    const double a_cdf = a.Cdf(min_value + (i + 1) * step);
    const double b_cdf = b.Cdf(min_value + (i + 1) * step);
    max_diff = std::max(max_diff, std::abs(a_cdf - b_cdf));
  }
  return max_diff;
}

double KolmogorovSmirnovThreshold(double alpha, double a_count,
                                  double b_count) {
  const double sample_scaling = a_count * b_count / (a_count + b_count);
  return std::sqrt(-0.5 * std::log(alpha / 2) * sample_scaling);
}

}  // namespace grpc_core
