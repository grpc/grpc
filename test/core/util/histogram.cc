//
//
// Copyright 2015 gRPC authors.
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
//
//

#include <grpc/support/port_platform.h>

#include "test/core/util/histogram.h"

#include <math.h>
#include <stddef.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"

// Histograms are stored with exponentially increasing bucket sizes.
// The first bucket is [0, m) where m = 1 + resolution
// Bucket n (n>=1) contains [m**n, m**(n+1))
// There are sufficient buckets to reach max_bucket_start

struct grpc_histogram {
  // Sum of all values seen so far
  double sum;
  // Sum of squares of all values seen so far
  double sum_of_squares;
  // number of values seen so far
  double count;
  // m in the description
  double multiplier;
  double one_on_log_multiplier;
  // minimum value seen
  double min_seen;
  // maximum value seen
  double max_seen;
  // maximum representable value
  double max_possible;
  // number of buckets
  size_t num_buckets;
  // the buckets themselves
  uint32_t* buckets;
};

// determine a bucket index given a value - does no bounds checking
static size_t bucket_for_unchecked(grpc_histogram* h, double x) {
  return static_cast<size_t>(log(x) * h->one_on_log_multiplier);
}

// bounds checked version of the above
static size_t bucket_for(grpc_histogram* h, double x) {
  size_t bucket =
      bucket_for_unchecked(h, grpc_core::Clamp(x, 1.0, h->max_possible));
  GPR_ASSERT(bucket < h->num_buckets);
  return bucket;
}

// at what value does a bucket start?
static double bucket_start(grpc_histogram* h, double x) {
  return pow(h->multiplier, x);
}

grpc_histogram* grpc_histogram_create(double resolution,
                                      double max_bucket_start) {
  grpc_histogram* h =
      static_cast<grpc_histogram*>(gpr_malloc(sizeof(grpc_histogram)));
  GPR_ASSERT(resolution > 0.0);
  GPR_ASSERT(max_bucket_start > resolution);
  h->sum = 0.0;
  h->sum_of_squares = 0.0;
  h->multiplier = 1.0 + resolution;
  h->one_on_log_multiplier = 1.0 / log(1.0 + resolution);
  h->max_possible = max_bucket_start;
  h->count = 0.0;
  h->min_seen = max_bucket_start;
  h->max_seen = 0.0;
  h->num_buckets = bucket_for_unchecked(h, max_bucket_start) + 1;
  GPR_ASSERT(h->num_buckets > 1);
  GPR_ASSERT(h->num_buckets < 100000000);
  h->buckets =
      static_cast<uint32_t*>(gpr_zalloc(sizeof(uint32_t) * h->num_buckets));
  return h;
}

void grpc_histogram_destroy(grpc_histogram* h) {
  gpr_free(h->buckets);
  gpr_free(h);
}

void grpc_histogram_add(grpc_histogram* h, double x) {
  h->sum += x;
  h->sum_of_squares += x * x;
  h->count++;
  if (x < h->min_seen) {
    h->min_seen = x;
  }
  if (x > h->max_seen) {
    h->max_seen = x;
  }
  h->buckets[bucket_for(h, x)]++;
}

int grpc_histogram_merge(grpc_histogram* dst, const grpc_histogram* src) {
  if ((dst->num_buckets != src->num_buckets) ||
      (dst->multiplier != src->multiplier)) {
    // Fail because these histograms don't match
    return 0;
  }
  grpc_histogram_merge_contents(dst, src->buckets, src->num_buckets,
                                src->min_seen, src->max_seen, src->sum,
                                src->sum_of_squares, src->count);
  return 1;
}

void grpc_histogram_merge_contents(grpc_histogram* histogram,
                                   const uint32_t* data, size_t data_count,
                                   double min_seen, double max_seen, double sum,
                                   double sum_of_squares, double count) {
  size_t i;
  GPR_ASSERT(histogram->num_buckets == data_count);
  histogram->sum += sum;
  histogram->sum_of_squares += sum_of_squares;
  histogram->count += count;
  if (min_seen < histogram->min_seen) {
    histogram->min_seen = min_seen;
  }
  if (max_seen > histogram->max_seen) {
    histogram->max_seen = max_seen;
  }
  for (i = 0; i < histogram->num_buckets; i++) {
    histogram->buckets[i] += data[i];
  }
}

static double threshold_for_count_below(grpc_histogram* h, double count_below) {
  double count_so_far;
  double lower_bound;
  double upper_bound;
  size_t lower_idx;
  size_t upper_idx;

  if (h->count == 0) {
    return 0.0;
  }

  if (count_below <= 0) {
    return h->min_seen;
  }
  if (count_below >= h->count) {
    return h->max_seen;
  }

  // find the lowest bucket that gets us above count_below
  count_so_far = 0.0;
  for (lower_idx = 0; lower_idx < h->num_buckets; lower_idx++) {
    count_so_far += h->buckets[lower_idx];
    if (count_so_far >= count_below) {
      break;
    }
  }
  if (count_so_far == count_below) {
    // this bucket hits the threshold exactly... we should be midway through
    // any run of zero values following the bucket
    for (upper_idx = lower_idx + 1; upper_idx < h->num_buckets; upper_idx++) {
      if (h->buckets[upper_idx]) {
        break;
      }
    }
    return (bucket_start(h, static_cast<double>(lower_idx)) +
            bucket_start(h, static_cast<double>(upper_idx))) /
           2.0;
  } else {
    // treat values as uniform throughout the bucket, and find where this value
    // should lie
    lower_bound = bucket_start(h, static_cast<double>(lower_idx));
    upper_bound = bucket_start(h, static_cast<double>(lower_idx + 1));
    return grpc_core::Clamp(upper_bound - (upper_bound - lower_bound) *
                                              (count_so_far - count_below) /
                                              h->buckets[lower_idx],
                            h->min_seen, h->max_seen);
  }
}

double grpc_histogram_percentile(grpc_histogram* h, double percentile) {
  return threshold_for_count_below(h, h->count * percentile / 100.0);
}

double grpc_histogram_mean(grpc_histogram* h) {
  GPR_ASSERT(h->count != 0);
  return h->sum / h->count;
}

double grpc_histogram_stddev(grpc_histogram* h) {
  return sqrt(grpc_histogram_variance(h));
}

double grpc_histogram_variance(grpc_histogram* h) {
  if (h->count == 0) return 0.0;
  return (h->sum_of_squares * h->count - h->sum * h->sum) /
         (h->count * h->count);
}

double grpc_histogram_maximum(grpc_histogram* h) { return h->max_seen; }

double grpc_histogram_minimum(grpc_histogram* h) { return h->min_seen; }

double grpc_histogram_count(grpc_histogram* h) { return h->count; }

double grpc_histogram_sum(grpc_histogram* h) { return h->sum; }

double grpc_histogram_sum_of_squares(grpc_histogram* h) {
  return h->sum_of_squares;
}

const uint32_t* grpc_histogram_get_contents(grpc_histogram* histogram,
                                            size_t* count) {
  *count = histogram->num_buckets;
  return histogram->buckets;
}
