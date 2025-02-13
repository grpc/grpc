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

#include "src/core/util/tdigest.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/random/random.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::DoubleNear;
using testing::NanSensitiveDoubleEq;

namespace grpc_core {
namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

double GetTrueQuantile(const std::vector<double>& samples, double quantile) {
  std::vector<double> s = samples;
  double true_idx = static_cast<double>(s.size()) * quantile - 1;
  double idx_left = std::floor(true_idx);
  double idx_right = std::ceil(true_idx);

  std::sort(s.begin(), s.end());
  if (idx_left == idx_right) {
    return s[idx_left];
  }
  return s[idx_left] * (idx_right - true_idx) +
         s[idx_right] * (true_idx - idx_left);
}

double GetTrueCdf(const std::vector<double>& samples, double val) {
  std::vector<double> s = samples;
  std::sort(s.begin(), s.end());

  if (val < s.front()) {
    return 0;
  }
  if (val >= s.back()) {
    return 1;
  }

  int true_idx = 0;
  for (double v : s) {
    if (v > val) {
      break;
    }
    ++true_idx;
  }

  return static_cast<double>(true_idx) / static_cast<double>(samples.size());
}

}  // namespace

TEST(TDigestTest, Reset) {
  TDigest tdigest(100);
  EXPECT_EQ(tdigest.Compression(), 100);

  tdigest.Reset(50);
  EXPECT_EQ(tdigest.Compression(), 50);

  tdigest.Reset(20);
  EXPECT_EQ(tdigest.Compression(), 20);
}

TEST(TDigestTest, Stats) {
  TDigest tdigest(100);
  tdigest.Add(10);
  EXPECT_EQ(1, tdigest.Count());
  EXPECT_EQ(10, tdigest.Min());
  EXPECT_EQ(10, tdigest.Max());
  EXPECT_EQ(10, tdigest.Sum());
  EXPECT_EQ(100, tdigest.Compression());

  tdigest.Add(20);
  EXPECT_EQ(2, tdigest.Count());
  EXPECT_EQ(10, tdigest.Min());
  EXPECT_EQ(20, tdigest.Max());
  EXPECT_EQ(30, tdigest.Sum());
  EXPECT_EQ(100, tdigest.Compression());
}

TEST(TDigestTest, MergeMultipleIntoSingleValued) {
  constexpr double kMerges = 100;
  constexpr double kCompression = 100;

  TDigest tdigest(kCompression);

  auto p01 = tdigest.Quantile(.01);
  auto p50 = tdigest.Quantile(.50);
  auto p99 = tdigest.Quantile(.99);
  EXPECT_THAT(p01, NanSensitiveDoubleEq(kNan));
  EXPECT_THAT(p50, NanSensitiveDoubleEq(kNan));
  EXPECT_THAT(p99, NanSensitiveDoubleEq(kNan));

  for (int i = 0; i < kMerges; i++) {
    TDigest new_tdigest(kCompression);
    new_tdigest.Add(10);
    new_tdigest.Merge(tdigest);
    new_tdigest.Swap(tdigest);
  }

  EXPECT_EQ(kMerges, tdigest.Count());
  p01 = tdigest.Quantile(.01);
  p50 = tdigest.Quantile(.50);
  p99 = tdigest.Quantile(.99);
  EXPECT_THAT(p01, NanSensitiveDoubleEq(10));
  EXPECT_THAT(p50, NanSensitiveDoubleEq(10));
  EXPECT_THAT(p99, NanSensitiveDoubleEq(10));
}

TEST(TDigestTest, MergeSingleIntoMultipleValued) {
  constexpr double kMerges = 100;
  constexpr double kCompression = 100;

  TDigest tdigest(kCompression);

  auto p01 = tdigest.Quantile(.01);
  auto p50 = tdigest.Quantile(.50);
  auto p99 = tdigest.Quantile(.99);
  EXPECT_THAT(p01, NanSensitiveDoubleEq(kNan));
  EXPECT_THAT(p50, NanSensitiveDoubleEq(kNan));
  EXPECT_THAT(p99, NanSensitiveDoubleEq(kNan));

  for (int i = 0; i < kMerges; i++) {
    TDigest new_tdigest(kCompression);
    new_tdigest.Add(10);
    tdigest.Merge(new_tdigest);
  }

  EXPECT_EQ(kMerges, tdigest.Count());
  p01 = tdigest.Quantile(.01);
  p50 = tdigest.Quantile(.50);
  p99 = tdigest.Quantile(.99);
  EXPECT_THAT(p01, NanSensitiveDoubleEq(10));
  EXPECT_THAT(p50, NanSensitiveDoubleEq(10));
  EXPECT_THAT(p99, NanSensitiveDoubleEq(10));
}

TEST(TDigestTest, CdfBetweenLastCentroidAndMax) {
  // Make sure we return the correct CDF value for an element between the last
  // centroid and maximum.
  constexpr double kCompression = 10;
  TDigest tdigest(kCompression);

  for (int i = 1; i <= 100; i++) {
    tdigest.Add(i);
  }
  for (int i = 1; i <= 100; i++) {
    tdigest.Add(i * 100);
  }
  for (int i = 1; i <= 100; i++) {
    tdigest.Add(i * 200);
  }

  auto cdf_min = tdigest.Cdf(1);
  EXPECT_THAT(cdf_min, NanSensitiveDoubleEq(0));
  auto cdf_max = tdigest.Cdf(20000);
  EXPECT_THAT(cdf_max, NanSensitiveDoubleEq(1));
  auto cdf_tail = tdigest.Cdf(20000 - 1);
  EXPECT_THAT(cdf_tail, DoubleNear(0.9999, 1e-4));
}

TEST(TDigestTest, CdfMostlyMin) {
  // Make sure we return the correct CDF value when most samples are the
  // minimum value.
  constexpr double kCompression = 10;
  constexpr double kMin = 0;
  constexpr double kMax = 1;
  TDigest tdigest(kCompression);

  for (int i = 0; i < 100; i++) {
    tdigest.Add(kMin);
  }
  tdigest.Add(kMax);

  auto cdf_min = tdigest.Cdf(kMin);
  EXPECT_THAT(cdf_min, DoubleNear(0.98, 1e-3));
  auto cdf_max = tdigest.Cdf(kMax);
  EXPECT_THAT(cdf_max, NanSensitiveDoubleEq(1));
}

TEST(TDigestTest, SingletonInACrowd) {
  // Add a test case similar to what is reported upstream:
  // https://github.com/tdunning/t-digest/issues/89
  //
  // We want to make sure when we have 10k samples of a specific number,
  // we do not lose information about a single sample at the tail.
  constexpr int kCrowdSize = 10000;
  constexpr double kCompression = 100;

  TDigest tdigest(kCompression);

  for (int i = 0; i < kCrowdSize; i++) {
    tdigest.Add(10);
  }
  tdigest.Add(20);

  EXPECT_THAT(tdigest.Quantile(0), NanSensitiveDoubleEq(10));
  EXPECT_THAT(tdigest.Quantile(0.5), NanSensitiveDoubleEq(10));
  EXPECT_THAT(tdigest.Quantile(0.8), NanSensitiveDoubleEq(10));
  EXPECT_THAT(tdigest.Quantile(0.9), NanSensitiveDoubleEq(10));
  EXPECT_THAT(tdigest.Quantile(0.99), NanSensitiveDoubleEq(10));
  EXPECT_THAT(tdigest.Quantile(0.999), NanSensitiveDoubleEq(10));
  EXPECT_THAT(tdigest.Quantile(1), NanSensitiveDoubleEq(20));
}

TEST(TDigestTest, MergeDifferentCompressions) {
  TDigest tdigest_1(100);
  TDigest tdigest_2(200);
  for (int i = 0; i < 10; ++i) {
    tdigest_1.Add(1);
    tdigest_2.Add(2);
  }

  {
    // Compression/discrete should remain "to be determined".
    TDigest tdigest_0(0);
    tdigest_0.Merge(tdigest_0);
    EXPECT_EQ(0, tdigest_0.Compression());
  }

  {
    // Should take compression/discrete of tdigest_1.
    TDigest tdigest_0(0);
    tdigest_0.Merge(tdigest_1);
    EXPECT_EQ(100, tdigest_0.Compression());
    EXPECT_EQ(1, tdigest_0.Max());
    EXPECT_EQ(10, tdigest_0.Count());
  }

  {
    // Should take compression/discrete of tdigest_2.
    TDigest tdigest_0(0);
    tdigest_0.Merge(tdigest_2);
    EXPECT_EQ(200, tdigest_0.Compression());
    EXPECT_EQ(2, tdigest_0.Max());
    EXPECT_EQ(10, tdigest_0.Count());

    // Now should succeed without changing compression/discrete.
    tdigest_0.Merge(tdigest_1);
    EXPECT_EQ(200, tdigest_0.Compression());
    EXPECT_EQ(2, tdigest_0.Max());
    EXPECT_EQ(20, tdigest_0.Count());
  }
}

// Sample generators for Cdf() and Percentile() precision tests.

// Returns a vector of `val` repeated `count` times.
std::vector<double> ConstantSamples(int count, double val) {
  std::vector<double> v;
  v.reserve(count);
  for (int i = 0; i < count; ++i) {
    v.push_back(val);
  }
  return v;
}

// Returns a vector of `count` number of samples from Normal(mean, stdev.
std::vector<double> NormalSamples(int count, double mean, double stdev) {
  std::vector<double> v;
  v.reserve(count);
  absl::BitGen rng;
  for (int i = 0; i < count; i++) {
    v.push_back(absl::Gaussian(rng, mean, stdev));
  }
  return v;
}

// Returns a vector of `count` number of samples drawn uniformly randomly in
// range [from, to].
template <typename T = double>
std::vector<double> UniformSamples(int count, double from, double to) {
  std::vector<double> v;
  v.reserve(count);
  absl::BitGen rng;
  for (int i = 0; i < count; i++) {
    v.push_back(static_cast<T>(absl::Uniform(rng, from, to)));
  }
  return v;
}

struct PrecisionTestParam {
  // Function to get samples vector.
  std::function<std::vector<double>()> generate_samples;
  // Map of {percentile or val, max error bound}.
  absl::flat_hash_map<double, double> max_errors;
};

class QuantilePrecisionTest
    : public ::testing::TestWithParam<PrecisionTestParam> {
 public:
  static constexpr double kCompression = 100;
  static constexpr double kMaxCentroids = 200;
};

// Tests max and average Percentile() errors against the true percentile.
TEST_P(QuantilePrecisionTest, QuantilePrecisionTest) {
  // We expect higher precision near both ends.
  const absl::flat_hash_map<double, double>& max_error_bounds =
      GetParam().max_errors;

  const std::vector<double> samples = GetParam().generate_samples();
  TDigest tdigest(kCompression);
  for (double i : samples) {
    tdigest.Add(i);
  }

  for (const auto& p : max_error_bounds) {
    double quantile = p.first;
    double error =
        abs(tdigest.Quantile(quantile) - GetTrueQuantile(samples, quantile));
    EXPECT_LE(error, p.second)
        << "(quantile=" << quantile << ") actual:" << tdigest.Quantile(quantile)
        << " expected:" << GetTrueQuantile(samples, quantile);
  }
}

// Same as above but merge every 1000 samples.
TEST_P(QuantilePrecisionTest, MergeQuantilePrecisionTest) {
  // We expect higher precision near both ends.
  const absl::flat_hash_map<double, double>& max_error_bounds =
      GetParam().max_errors;

  const std::vector<double> samples = GetParam().generate_samples();
  TDigest tdigest(kCompression);
  TDigest temp(kCompression);
  for (double i : samples) {
    temp.Add(i);
    if (temp.Count() == 1000) {
      tdigest.Merge(temp);
      temp.Reset(kCompression);
    }
  }
  tdigest.Merge(temp);

  ASSERT_EQ(tdigest.Count(), samples.size());
  for (const auto& p : max_error_bounds) {
    double quantile = p.first;
    double error =
        abs(tdigest.Quantile(quantile) - GetTrueQuantile(samples, quantile));
    EXPECT_LE(error, p.second)
        << "(quantile=" << quantile << ") actual:" << tdigest.Quantile(quantile)
        << " expected:" << GetTrueQuantile(samples, quantile);
  }
}

INSTANTIATE_TEST_SUITE_P(
    QuantilePrecisionTest, QuantilePrecisionTest,
    ::testing::Values(
        // Constant
        PrecisionTestParam{[]() { return ConstantSamples(100000, 10); },
                           {{0.01, 0}, {0.5, 0}, {0.99, 0}}},

        // Continuous samples
        PrecisionTestParam{[]() { return NormalSamples(100000, 0, 5); },
                           {{0.01, 0.5}, {0.5, 1}, {0.99, 0.5}}},
        PrecisionTestParam{[]() { return UniformSamples(100000, -5000, 5000); },
                           {{0.01, 22}, {0.5, 70}, {0.99, 22}}}));

class CdfPrecisionTest : public ::testing::TestWithParam<PrecisionTestParam> {
 public:
  static constexpr double kCompression = 100;
  static constexpr double kMaxCentroids = 200;
};

// Tests max and average Percentile() errors against the true percentile.
TEST_P(CdfPrecisionTest, CdfPrecisionTest) {
  // We expect higher precision near both ends.
  const absl::flat_hash_map<double, double>& max_error_bounds =
      GetParam().max_errors;

  const std::vector<double> samples = GetParam().generate_samples();
  TDigest tdigest(kCompression);
  for (double i : samples) {
    tdigest.Add(i);
  }

  ASSERT_EQ(tdigest.Count(), samples.size());
  for (const auto& p : max_error_bounds) {
    double val = p.first;
    double error = abs(tdigest.Cdf(val) - GetTrueCdf(samples, val));
    EXPECT_LE(error, p.second)
        << "(val=" << val << ") actual:" << tdigest.Cdf(val)
        << " expected:" << GetTrueCdf(samples, val);
  }
}

// Same as above but merge every 1000 samples.
TEST_P(CdfPrecisionTest, MergeCdfPrecisionTest) {
  // We expect higher precision near both ends.
  const absl::flat_hash_map<double, double>& max_error_bounds =
      GetParam().max_errors;

  const std::vector<double> samples = GetParam().generate_samples();
  TDigest tdigest(kCompression);
  TDigest temp(kCompression);
  for (double i : samples) {
    temp.Add(i);
    if (temp.Count() == 1000) {
      tdigest.Merge(temp);
      temp.Reset(kCompression);
    }
  }
  tdigest.Merge(temp);

  ASSERT_EQ(tdigest.Count(), samples.size());
  for (const auto& p : max_error_bounds) {
    double val = p.first;
    double error = abs(tdigest.Cdf(val) - GetTrueCdf(samples, val));
    EXPECT_LE(error, p.second)
        << "(val=" << val << ") actual:" << tdigest.Cdf(val)
        << " expected:" << GetTrueCdf(samples, val);
  }
}

INSTANTIATE_TEST_SUITE_P(
    CdfPrecisionTest, CdfPrecisionTest,
    ::testing::Values(
        // Constant.
        PrecisionTestParam{[]() { return ConstantSamples(100000, 10); },
                           {{9, 0}, {10, 0}, {11, 0}}},

        // Continuous samples.
        PrecisionTestParam{[]() { return NormalSamples(100000, 0, 5); },
                           {{-10, 0.005}, {0.0, 0.006}, {10, 0.005}}},
        PrecisionTestParam{[]() { return UniformSamples(100000, -5000, 5000); },
                           {{-5000.1, 0},
                            {-4900, 0.005},
                            {0, 0.007},
                            {4900, 0.005},
                            {5000, 0}}},

        // Dense and sparse samples.
        PrecisionTestParam{
            []() { return UniformSamples<int>(100000, 0, 10); },
            {{-0.01, 0}, {0.01, 0.03}, {5.0, 0.05}, {9.99, 0.03}, {10, 0}}},
        PrecisionTestParam{
            []() { return UniformSamples<int>(100000, -10000, 10000); },
            {{-10001, 0},
             {-9900, 0.005},
             {0, 0.008},
             {9900, 0.005},
             {10000, 0}}}));

TEST(TDigestEmptyStringTest, Test) {
  TDigest tdigest(0);
  ASSERT_TRUE(tdigest.FromString("").ok());
  EXPECT_EQ(tdigest.ToString(), "0/0/0/0/0");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
