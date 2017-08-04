/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_SUPPORT_HISTOGRAM_H
#define GRPC_SUPPORT_HISTOGRAM_H

#include <grpc/support/port_platform.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gpr_histogram gpr_histogram;

GPRAPI gpr_histogram *gpr_histogram_create(double resolution,
                                           double max_bucket_start);
GPRAPI void gpr_histogram_destroy(gpr_histogram *h);
GPRAPI void gpr_histogram_add(gpr_histogram *h, double x);

/** The following merges the second histogram into the first. It only works
   if they have the same buckets and resolution. Returns 0 on failure, 1
   on success */
GPRAPI int gpr_histogram_merge(gpr_histogram *dst, const gpr_histogram *src);

GPRAPI double gpr_histogram_percentile(gpr_histogram *histogram,
                                       double percentile);
GPRAPI double gpr_histogram_mean(gpr_histogram *histogram);
GPRAPI double gpr_histogram_stddev(gpr_histogram *histogram);
GPRAPI double gpr_histogram_variance(gpr_histogram *histogram);
GPRAPI double gpr_histogram_maximum(gpr_histogram *histogram);
GPRAPI double gpr_histogram_minimum(gpr_histogram *histogram);
GPRAPI double gpr_histogram_count(gpr_histogram *histogram);
GPRAPI double gpr_histogram_sum(gpr_histogram *histogram);
GPRAPI double gpr_histogram_sum_of_squares(gpr_histogram *histogram);

GPRAPI const uint32_t *gpr_histogram_get_contents(gpr_histogram *histogram,
                                                  size_t *count);
GPRAPI void gpr_histogram_merge_contents(gpr_histogram *histogram,
                                         const uint32_t *data,
                                         size_t data_count, double min_seen,
                                         double max_seen, double sum,
                                         double sum_of_squares, double count);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_SUPPORT_HISTOGRAM_H */
