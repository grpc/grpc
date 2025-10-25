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
#include <optional>
#include <vector>

#include "src/core/util/grpc_check.h"
#include "absl/log/log.h"
#include "absl/strings/str_join.h"
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
using HistogramBuckets = absl::Span<const int64_t>;

// Returns the bucket index for the given value in the given bounds.
// The bounds must be sorted in ascending order.
// The bounds array holds the upper bound of the bucket with the given index.
inline int64_t BucketInBoundsFor(absl::Span<const int64_t> bounds,
                                 int64_t value) {
  GRPC_CHECK(!bounds.empty());
  if (value < bounds[0]) return 0;
  if (value > bounds.back()) return bounds.size() - 1;
  // Find the first element in bounds strictly greater than value
  auto it = std::upper_bound(bounds.begin(), bounds.end(), value);
  if (it == bounds.end()) return bounds.size() - 1;
  // The bucket index is the index of the element just before it.
  return std::distance(bounds.begin(), it);
}

class LinearHistogramShape {
 public:
  LinearHistogramShape(int64_t min, int64_t max) : min_(min), max_(max) {}

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

class ExponentialHistogramShape {
 public:
  ExponentialHistogramShape(int64_t max, size_t buckets)
      : max_(max), buckets_(buckets) {
    first_non_trivial_ = -1;
    GRPC_CHECK_GT(max, 0);
    GRPC_CHECK_LT(buckets, 1000000000u);
    if (max <= static_cast<int64_t>(buckets)) {
      for (size_t i = 0; i < static_cast<size_t>(max); ++i) {
        bounds_.push_back(i + 1);
      }
      first_non_trivial_ = max;
      buckets_ = max;
      return;
    }
    int64_t first_bucket = std::ceil(std::pow(max, 1.0 / (buckets_ + 1)));
    if (first_bucket <= 1) first_bucket = 1;
    if (first_bucket != 1) first_non_trivial_ = 0;
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
      } else if (first_non_trivial_ == -1) {
        first_non_trivial_ = bounds_.size();
      }
      bounds_.push_back(nextb);
    }
    GRPC_CHECK_EQ(bounds_.size(), buckets_);
    if (first_non_trivial_ == -1) {
      first_non_trivial_ = max_;
      offset_ = 0;
      shift_ = 0;
      return;
    }
    offset_ = DoubleToUint(first_non_trivial_);
    for (shift_ = 63; shift_ > 0; --shift_) {
      bool found_alias = false;
      for (size_t i = first_non_trivial_ + 1; i < bounds_.size(); ++i) {
        if ((DoubleToUint(bounds_[i]) - offset_) >> shift_ ==
            (DoubleToUint(bounds_[i - 1]) - offset_) >> shift_) {
          found_alias = true;
          break;
        }
      }
      if (!found_alias) {
        break;
      }
    }
    GRPC_CHECK_GE(shift_, 0u);
    GRPC_CHECK_LT(shift_, 64u);
    for (size_t i = 0; i <= (DoubleToUint(max_) - offset_) >> shift_; ++i) {
      lookup_table_.push_back(
          BucketInBoundsFor(bounds_, UintToDouble((i << shift_) + offset_)));
    }
  }

  ExponentialHistogramShape(const ExponentialHistogramShape&) = delete;
  ExponentialHistogramShape& operator=(const ExponentialHistogramShape&) =
      delete;
  ExponentialHistogramShape(ExponentialHistogramShape&&) = default;
  ExponentialHistogramShape& operator=(ExponentialHistogramShape&&) = default;

  size_t buckets() const { return buckets_; }
  size_t BucketFor(int64_t value) const {
    if (value >= max_) return buckets_ - 1;
    if (value < first_non_trivial_) {
      if (value < 0) return 0;
      return value;
    }
    auto index = (DoubleToUint(value) - offset_) >> shift_;
    size_t bucket = lookup_table_[index];
    GRPC_DCHECK_LT(bucket, buckets_) << absl::StrJoin(lookup_table_, ",");
    GRPC_DCHECK_LT(bucket, bounds_.size()) << absl::StrJoin(bounds_, ",");
    while (bucket < bounds_.size() - 1 && value >= bounds_[bucket]) {
      ++bucket;
    }
    while (bucket > 0 && value < bounds_[bucket - 1]) {
      --bucket;
    }
    GRPC_DCHECK_LT(value, bounds_[bucket]);
    return bucket;
  }

  HistogramBuckets bounds() const { return bounds_; }
  absl::Span<const size_t> lookup_table() const { return lookup_table_; }

 private:
  union DblUint {
    double dbl;
    uint64_t uint;
  };

  static double UintToDouble(uint64_t x) {
    union DblUint {
      double dbl;
      uint64_t uint;
    };
    DblUint val;
    val.uint = x;
    return val.dbl;
  }

  static uint64_t DoubleToUint(double x) {
    union DblUint {
      double dbl;
      uint64_t uint;
    };
    DblUint val;
    val.dbl = x;
    return val.uint;
  }

  int64_t max_;
  int64_t first_non_trivial_;
  uint64_t offset_;
  uint64_t shift_;
  std::vector<size_t> lookup_table_;
  std::vector<int64_t> bounds_;
  size_t buckets_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TELEMETRY_HISTOGRAM_H
