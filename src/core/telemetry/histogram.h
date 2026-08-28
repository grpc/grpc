// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_TELEMETRY_HISTOGRAM_H
#define GRPC_SRC_CORE_TELEMETRY_HISTOGRAM_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "src/core/util/grpc_check.h"
#include "absl/log/log.h"
#include "absl/types/span.h"

namespace grpc_core {

// Bucket layout for a histogram.
//
// The bucket layout is a vector of bucket boundaries. The bucket with index i
// collects values in the half-open interval [bounds[i-1], bounds[i]).
//
// Bucket 0 includes all values less than bounds[0]. Similarly, bucket
// bounds.size() - 1 includes all values greater than bounds.back().
//
// The bucket layout must be sorted in ascending order.
using Int64HistogramBuckets = absl::Span<const int64_t>;
using DoubleHistogramBuckets = absl::Span<const double>;

// Returns the bucket index for the given value in the given bounds.
// The bounds must be sorted in ascending order.
// The bounds array holds the upper bound of the bucket with the given index.
template <typename T>
size_t BucketInBoundsFor(absl::Span<const T> bounds, T value) {
  GRPC_CHECK(!bounds.empty());
  if constexpr (std::is_floating_point_v<T>) {
    if (std::isnan(value)) {
      LOG_EVERY_N_SEC(WARNING, 1)
          << "Attempted to add a NaN value in a DoubleHistogram. Adding it to "
             "the last bucket.";
      return bounds.size() - 1;
    }
  }
  if (value < bounds[0]) return 0;
  if (value > bounds.back()) return bounds.size() - 1;
  // Find the first element in bounds strictly greater than value
  auto it = std::upper_bound(bounds.begin(), bounds.end(), value);
  if (it == bounds.end()) return bounds.size() - 1;
  // The bucket index is the index of the element just before it.
  return static_cast<size_t>(std::distance(bounds.begin(), it));
}

class LinearInt64HistogramShape {
 public:
  LinearInt64HistogramShape(int64_t min, int64_t max) : min_(min), max_(max) {}

  size_t buckets() const { return max_ - min_ + 1; }
  size_t BucketFor(int64_t value) const {
    if (value < min_) return 0;
    if (value > max_) return buckets() - 1;
    return value - min_;
  }

 private:
  int64_t min_;
  int64_t max_;
};

class LinearDoubleHistogramShape {
 public:
  LinearDoubleHistogramShape(double min, double max, size_t buckets) {
    GRPC_CHECK_GT(buckets, 0u);
    GRPC_CHECK_GT(max, min);
    bounds_.reserve(buckets);
    double step = (max - min) / static_cast<double>(buckets);
    for (size_t i = 1; i < buckets; ++i) {
      bounds_.push_back(min + static_cast<double>(i) * step);
    }
    bounds_.push_back(max);
  }

  LinearDoubleHistogramShape(const LinearDoubleHistogramShape&) = delete;
  LinearDoubleHistogramShape& operator=(const LinearDoubleHistogramShape&) =
      delete;
  LinearDoubleHistogramShape(LinearDoubleHistogramShape&&) = default;
  LinearDoubleHistogramShape& operator=(LinearDoubleHistogramShape&&) = default;

  size_t buckets() const { return bounds_.size(); }
  size_t BucketFor(double value) const {
    return BucketInBoundsFor(absl::MakeConstSpan(bounds_), value);
  }

  DoubleHistogramBuckets bounds() const { return bounds_; }

 private:
  std::vector<double> bounds_;
};

class ExponentialInt64HistogramShape {
 public:
  ExponentialInt64HistogramShape(int64_t max, size_t buckets) {
    GRPC_CHECK_GT(max, 0);
    // Increase if needed. As of June 2026, the maximum we are using is 100.
    // Note that the BucketFor has to do a binary search, so it's better to
    // keep the number of buckets small.
    GRPC_CHECK_LE(buckets, 512u);
    if (max <= static_cast<int64_t>(buckets)) {
      for (size_t i = 0; i < static_cast<size_t>(max); ++i) {
        bounds_.push_back(i + 1);
      }
      buckets = max;
      return;
    }
    int64_t first_bucket = std::ceil(std::pow(max, 1.0 / (buckets + 1)));
    if (first_bucket <= 1) first_bucket = 1;
    bounds_.push_back(first_bucket);
    while (bounds_.size() < buckets) {
      int64_t nextb;
      int64_t prevb = bounds_.empty() ? 0 : bounds_.back();
      if (bounds_.size() == buckets - 1) {
        nextb = max;
      } else {
        double mul = std::pow(static_cast<double>(max) / prevb,
                              1.0 / (buckets - bounds_.size()));
        nextb = static_cast<long long>(std::ceil(bounds_.back() * mul));
      }
      if (nextb <= bounds_.back() + 1) {
        nextb = bounds_.back() + 1;
      }
      bounds_.push_back(nextb);
    }
    GRPC_CHECK_EQ(bounds_.size(), buckets);
  }

  ExponentialInt64HistogramShape(const ExponentialInt64HistogramShape&) =
      delete;
  ExponentialInt64HistogramShape& operator=(
      const ExponentialInt64HistogramShape&) = delete;
  ExponentialInt64HistogramShape(ExponentialInt64HistogramShape&&) = default;
  ExponentialInt64HistogramShape& operator=(ExponentialInt64HistogramShape&&) =
      default;

  size_t buckets() const { return bounds_.size(); }
  size_t BucketFor(int64_t value) const {
    return BucketInBoundsFor(absl::MakeConstSpan(bounds_), value);
  }

  Int64HistogramBuckets bounds() const { return bounds_; }

 private:
  std::vector<int64_t> bounds_;
};

class ExponentialDoubleHistogramShape {
 public:
  ExponentialDoubleHistogramShape(double max, size_t buckets,
                                  double min = 1.0) {
    GRPC_CHECK_GT(min, 0.0);
    GRPC_CHECK_GT(max, min);
    GRPC_CHECK_GT(buckets, 0u);
    bounds_.reserve(buckets);
    if (buckets == 1) {
      bounds_.push_back(max);
      return;
    }
    double factor = std::pow(max / min, 1.0 / static_cast<double>(buckets - 1));
    for (size_t i = 0; i < buckets - 1; ++i) {
      bounds_.push_back(min * std::pow(factor, static_cast<double>(i)));
    }
    bounds_.push_back(max);
  }

  ExponentialDoubleHistogramShape(const ExponentialDoubleHistogramShape&) =
      delete;
  ExponentialDoubleHistogramShape& operator=(
      const ExponentialDoubleHistogramShape&) = delete;
  ExponentialDoubleHistogramShape(ExponentialDoubleHistogramShape&&) = default;
  ExponentialDoubleHistogramShape& operator=(
      ExponentialDoubleHistogramShape&&) = default;

  size_t buckets() const { return bounds_.size(); }
  size_t BucketFor(double value) const {
    return BucketInBoundsFor(absl::MakeConstSpan(bounds_), value);
  }

  DoubleHistogramBuckets bounds() const { return bounds_; }

 private:
  std::vector<double> bounds_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TELEMETRY_HISTOGRAM_H
