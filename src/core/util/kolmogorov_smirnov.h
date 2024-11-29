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

#ifndef KOLMOGOROV_SMIRNOV_H
#define KOLMOGOROV_SMIRNOV_H

#include "src/core/util/tdigest.h"

namespace grpc_core {

// Perform a Kolmogorov-Smirnov test to determine if two TDigests are
// significantly different (returns true), or not (returns false).
//
// alpha is a real numbered value between 0 and 1, representing the
// significance level of the test.
//
// num_samples is the number of cdf samples to take from each TDigest.
//
// Computational complexity roughly num_samples * (a.NumCentroids() +
// b.NumCentroids()).
bool KolmogorovSmirnovTest(TDigest& a, TDigest& b, double alpha,
                           uint32_t num_samples = 10);

double KolmogorovSmirnovStatistic(TDigest& a, TDigest& b,
                                  uint32_t num_samples = 10);

double KolmogorovSmirnovThreshold(double alpha, double a_count, double b_count);

}  // namespace grpc_core

#endif  // KOLMOGOROV_SMIRNOV_H
