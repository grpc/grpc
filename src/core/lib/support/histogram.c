/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/support/histogram.h>

#include <math.h>
#include <stddef.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>

/* Histograms are stored with exponentially increasing bucket sizes.
   The first bucket is [0, m) where m = 1 + resolution
   Bucket n (n>=1) contains [m**n, m**(n+1))
   There are sufficient buckets to reach max_bucket_start */

struct gpr_histogram {
  /* Sum of all values seen so far */
  double sum;
  /* Sum of squares of all values seen so far */
  double sum_of_squares;
  /* number of values seen so far */
  double count;
  /* m in the description */
  double multiplier;
  double one_on_log_multiplier;
  /* minimum value seen */
  double min_seen;
  /* maximum value seen */
  double max_seen;
  /* maximum representable value */
  double max_possible;
  /* number of buckets */
  size_t num_buckets;
  /* the buckets themselves */
  uint32_t *buckets;
};

/* determine a bucket index given a value - does no bounds checking */
static size_t bucket_for_unchecked(gpr_histogram *h, double x) {
  return (size_t)(log(x) * h->one_on_log_multiplier);
}

/* bounds checked version of the above */
static size_t bucket_for(gpr_histogram *h, double x) {
  size_t bucket = bucket_for_unchecked(h, GPR_CLAMP(x, 1.0, h->max_possible));
  GPR_ASSERT(bucket < h->num_buckets);
  return bucket;
}

/* at what value does a bucket start? */
static double bucket_start(gpr_histogram *h, double x) {
  return pow(h->multiplier, x);
}

gpr_histogram *gpr_histogram_create(double resolution,
                                    double max_bucket_start) {
  gpr_histogram *h = gpr_malloc(sizeof(gpr_histogram));
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
  h->buckets = gpr_zalloc(sizeof(uint32_t) * h->num_buckets);
  return h;
}

void gpr_histogram_destroy(gpr_histogram *h) {
  gpr_free(h->buckets);
  gpr_free(h);
}

void gpr_histogram_add(gpr_histogram *h, double x) {
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

int gpr_histogram_merge(gpr_histogram *dst, const gpr_histogram *src) {
  if ((dst->num_buckets != src->num_buckets) ||
      (dst->multiplier != src->multiplier)) {
    /* Fail because these histograms don't match */
    return 0;
  }
  gpr_histogram_merge_contents(dst, src->buckets, src->num_buckets,
                               src->min_seen, src->max_seen, src->sum,
                               src->sum_of_squares, src->count);
  return 1;
}

void gpr_histogram_merge_contents(gpr_histogram *dst, const uint32_t *data,
                                  size_t data_count, double min_seen,
                                  double max_seen, double sum,
                                  double sum_of_squares, double count) {
  size_t i;
  GPR_ASSERT(dst->num_buckets == data_count);
  dst->sum += sum;
  dst->sum_of_squares += sum_of_squares;
  dst->count += count;
  if (min_seen < dst->min_seen) {
    dst->min_seen = min_seen;
  }
  if (max_seen > dst->max_seen) {
    dst->max_seen = max_seen;
  }
  for (i = 0; i < dst->num_buckets; i++) {
    dst->buckets[i] += data[i];
  }
}

static double threshold_for_count_below(gpr_histogram *h, double count_below) {
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

  /* find the lowest bucket that gets us above count_below */
  count_so_far = 0.0;
  for (lower_idx = 0; lower_idx < h->num_buckets; lower_idx++) {
    count_so_far += h->buckets[lower_idx];
    if (count_so_far >= count_below) {
      break;
    }
  }
  if (count_so_far == count_below) {
    /* this bucket hits the threshold exactly... we should be midway through
       any run of zero values following the bucket */
    for (upper_idx = lower_idx + 1; upper_idx < h->num_buckets; upper_idx++) {
      if (h->buckets[upper_idx]) {
        break;
      }
    }
    return (bucket_start(h, (double)lower_idx) +
            bucket_start(h, (double)upper_idx)) /
           2.0;
  } else {
    /* treat values as uniform throughout the bucket, and find where this value
       should lie */
    lower_bound = bucket_start(h, (double)lower_idx);
    upper_bound = bucket_start(h, (double)(lower_idx + 1));
    return GPR_CLAMP(upper_bound -
                         (upper_bound - lower_bound) *
                             (count_so_far - count_below) /
                             h->buckets[lower_idx],
                     h->min_seen, h->max_seen);
  }
}

double gpr_histogram_percentile(gpr_histogram *h, double percentile) {
  return threshold_for_count_below(h, h->count * percentile / 100.0);
}

double gpr_histogram_mean(gpr_histogram *h) {
  GPR_ASSERT(h->count != 0);
  return h->sum / h->count;
}

double gpr_histogram_stddev(gpr_histogram *h) {
  return sqrt(gpr_histogram_variance(h));
}

double gpr_histogram_variance(gpr_histogram *h) {
  if (h->count == 0) return 0.0;
  return (h->sum_of_squares * h->count - h->sum * h->sum) /
         (h->count * h->count);
}

double gpr_histogram_maximum(gpr_histogram *h) { return h->max_seen; }

double gpr_histogram_minimum(gpr_histogram *h) { return h->min_seen; }

double gpr_histogram_count(gpr_histogram *h) { return h->count; }

double gpr_histogram_sum(gpr_histogram *h) { return h->sum; }

double gpr_histogram_sum_of_squares(gpr_histogram *h) {
  return h->sum_of_squares;
}

const uint32_t *gpr_histogram_get_contents(gpr_histogram *h, size_t *size) {
  *size = h->num_buckets;
  return h->buckets;
}
