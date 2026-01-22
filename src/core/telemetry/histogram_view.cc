// Copyright 2021 gRPC authors.
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

#include "src/core/telemetry/histogram_view.h"

#include <grpc/support/port_platform.h>

namespace grpc_core {

double HistogramView::Count() const {
  double sum = 0;
  for (int i = 0; i < num_buckets; i++) {
    sum += buckets[i];
  }
  return sum;
}

double HistogramView::ThresholdForCountBelow(double count_below) const {
  double lower_bound;
  double upper_bound;
  int upper_idx;

  // find the lowest bucket that gets us above count_below
  double count_so_far = 0.0;
  int lower_idx = 0;
  for (; lower_idx < num_buckets; lower_idx++) {
    count_so_far += static_cast<double>(buckets[lower_idx]);
    if (count_so_far >= count_below) {
      break;
    }
  }
  if (count_so_far == count_below) {
    // this bucket hits the threshold exactly... we should be midway through
    // any run of zero values following the bucket
    for (upper_idx = lower_idx + 1; upper_idx < num_buckets; upper_idx++) {
      if (buckets[upper_idx]) {
        break;
      }
    }
    return (bucket_boundaries[lower_idx] + bucket_boundaries[upper_idx]) / 2.0;
  } else {
    // treat values as uniform throughout the bucket, and find where this value
    // should lie
    lower_bound = bucket_boundaries[lower_idx];
    upper_bound = bucket_boundaries[lower_idx + 1];
    return upper_bound -
           ((upper_bound - lower_bound) * (count_so_far - count_below) /
            static_cast<double>(buckets[lower_idx]));
  }
}

double HistogramView::Percentile(double p) const {
  const double count = Count();
  if (count == 0) return 0.0;
  return ThresholdForCountBelow(count * p / 100.0);
}

}  // namespace grpc_core
