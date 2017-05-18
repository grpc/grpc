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
