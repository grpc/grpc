/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/debug/stats.h"

#include <inttypes.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/gpr/string.h"

grpc_stats_data* grpc_stats_per_cpu_storage = nullptr;
static size_t g_num_cores;

void grpc_stats_init(void) {
  g_num_cores = GPR_MAX(1, gpr_cpu_num_cores());
  grpc_stats_per_cpu_storage =
      (grpc_stats_data*)gpr_zalloc(sizeof(grpc_stats_data) * g_num_cores);
}

void grpc_stats_shutdown(void) { gpr_free(grpc_stats_per_cpu_storage); }

void grpc_stats_collect(grpc_stats_data* output) {
  memset(output, 0, sizeof(*output));
  for (size_t core = 0; core < g_num_cores; core++) {
    for (size_t i = 0; i < GRPC_STATS_COUNTER_COUNT; i++) {
      output->counters[i] += gpr_atm_no_barrier_load(
          &grpc_stats_per_cpu_storage[core].counters[i]);
    }
    for (size_t i = 0; i < GRPC_STATS_HISTOGRAM_BUCKETS; i++) {
      output->histograms[i] += gpr_atm_no_barrier_load(
          &grpc_stats_per_cpu_storage[core].histograms[i]);
    }
  }
}

void grpc_stats_diff(const grpc_stats_data* b, const grpc_stats_data* a,
                     grpc_stats_data* c) {
  for (size_t i = 0; i < GRPC_STATS_COUNTER_COUNT; i++) {
    c->counters[i] = b->counters[i] - a->counters[i];
  }
  for (size_t i = 0; i < GRPC_STATS_HISTOGRAM_BUCKETS; i++) {
    c->histograms[i] = b->histograms[i] - a->histograms[i];
  }
}

int grpc_stats_histo_find_bucket_slow(int value, const int* table,
                                      int table_size) {
  GRPC_STATS_INC_HISTOGRAM_SLOW_LOOKUPS();
  const int* const start = table;
  while (table_size > 0) {
    int step = table_size / 2;
    const int* it = table + step;
    if (value >= *it) {
      table = it + 1;
      table_size -= step + 1;
    } else {
      table_size = step;
    }
  }
  return (int)(table - start) - 1;
}

size_t grpc_stats_histo_count(const grpc_stats_data* stats,
                              grpc_stats_histograms histogram) {
  size_t sum = 0;
  for (int i = 0; i < grpc_stats_histo_buckets[histogram]; i++) {
    sum += (size_t)stats->histograms[grpc_stats_histo_start[histogram] + i];
  }
  return sum;
}

static double threshold_for_count_below(const gpr_atm* bucket_counts,
                                        const int* bucket_boundaries,
                                        int num_buckets, double count_below) {
  double count_so_far;
  double lower_bound;
  double upper_bound;
  int lower_idx;
  int upper_idx;

  /* find the lowest bucket that gets us above count_below */
  count_so_far = 0.0;
  for (lower_idx = 0; lower_idx < num_buckets; lower_idx++) {
    count_so_far += (double)bucket_counts[lower_idx];
    if (count_so_far >= count_below) {
      break;
    }
  }
  if (count_so_far == count_below) {
    /* this bucket hits the threshold exactly... we should be midway through
       any run of zero values following the bucket */
    for (upper_idx = lower_idx + 1; upper_idx < num_buckets; upper_idx++) {
      if (bucket_counts[upper_idx]) {
        break;
      }
    }
    return (bucket_boundaries[lower_idx] + bucket_boundaries[upper_idx]) / 2.0;
  } else {
    /* treat values as uniform throughout the bucket, and find where this value
       should lie */
    lower_bound = bucket_boundaries[lower_idx];
    upper_bound = bucket_boundaries[lower_idx + 1];
    return upper_bound - (upper_bound - lower_bound) *
                             (count_so_far - count_below) /
                             (double)bucket_counts[lower_idx];
  }
}

double grpc_stats_histo_percentile(const grpc_stats_data* stats,
                                   grpc_stats_histograms histogram,
                                   double percentile) {
  size_t count = grpc_stats_histo_count(stats, histogram);
  if (count == 0) return 0.0;
  return threshold_for_count_below(
      stats->histograms + grpc_stats_histo_start[histogram],
      grpc_stats_histo_bucket_boundaries[histogram],
      grpc_stats_histo_buckets[histogram], (double)count * percentile / 100.0);
}

char* grpc_stats_data_as_json(const grpc_stats_data* data) {
  gpr_strvec v;
  char* tmp;
  bool is_first = true;
  gpr_strvec_init(&v);
  gpr_strvec_add(&v, gpr_strdup("{"));
  for (size_t i = 0; i < GRPC_STATS_COUNTER_COUNT; i++) {
    gpr_asprintf(&tmp, "%s\"%s\": %" PRIdPTR, is_first ? "" : ", ",
                 grpc_stats_counter_name[i], data->counters[i]);
    gpr_strvec_add(&v, tmp);
    is_first = false;
  }
  for (size_t i = 0; i < GRPC_STATS_HISTOGRAM_COUNT; i++) {
    gpr_asprintf(&tmp, "%s\"%s\": [", is_first ? "" : ", ",
                 grpc_stats_histogram_name[i]);
    gpr_strvec_add(&v, tmp);
    for (int j = 0; j < grpc_stats_histo_buckets[i]; j++) {
      gpr_asprintf(&tmp, "%s%" PRIdPTR, j == 0 ? "" : ",",
                   data->histograms[grpc_stats_histo_start[i] + j]);
      gpr_strvec_add(&v, tmp);
    }
    gpr_asprintf(&tmp, "], \"%s_bkt\": [", grpc_stats_histogram_name[i]);
    gpr_strvec_add(&v, tmp);
    for (int j = 0; j < grpc_stats_histo_buckets[i]; j++) {
      gpr_asprintf(&tmp, "%s%d", j == 0 ? "" : ",",
                   grpc_stats_histo_bucket_boundaries[i][j]);
      gpr_strvec_add(&v, tmp);
    }
    gpr_strvec_add(&v, gpr_strdup("]"));
    is_first = false;
  }
  gpr_strvec_add(&v, gpr_strdup("}"));
  tmp = gpr_strvec_flatten(&v, nullptr);
  gpr_strvec_destroy(&v);
  return tmp;
}
