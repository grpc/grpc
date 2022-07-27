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

#include <grpc/support/port_platform.h>

#include "src/core/lib/debug/stats.h"

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>

namespace grpc_core {
Stats* const g_stats_data = [] {
  size_t num_cores = gpr_cpu_num_cores();
  Stats* stats = static_cast<Stats*>(
      gpr_zalloc(sizeof(Stats) + num_cores * sizeof(grpc_stats_data)));
  stats->num_cores = num_cores;
  return stats;
}();
}  // namespace grpc_core

void grpc_stats_collect(grpc_stats_data* output) {
  memset(output, 0, sizeof(*output));
  for (size_t core = 0; core < grpc_core::g_stats_data->num_cores; core++) {
    for (size_t i = 0; i < GRPC_STATS_COUNTER_COUNT; i++) {
      output->counters[i] += gpr_atm_no_barrier_load(
          &grpc_core::g_stats_data->per_cpu[core].counters[i]);
    }
    for (size_t i = 0; i < GRPC_STATS_HISTOGRAM_BUCKETS; i++) {
      output->histograms[i] += gpr_atm_no_barrier_load(
          &grpc_core::g_stats_data->per_cpu[core].histograms[i]);
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
  return static_cast<int>(table - start) - 1;
}

size_t grpc_stats_histo_count(const grpc_stats_data* stats,
                              grpc_stats_histograms histogram) {
  size_t sum = 0;
  for (int i = 0; i < grpc_stats_histo_buckets[histogram]; i++) {
    sum += static_cast<size_t>(
        stats->histograms[grpc_stats_histo_start[histogram] + i]);
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
    count_so_far += static_cast<double>(bucket_counts[lower_idx]);
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
                             static_cast<double>(bucket_counts[lower_idx]);
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
      grpc_stats_histo_buckets[histogram],
      static_cast<double>(count) * percentile / 100.0);
}

std::string grpc_stats_data_as_json(const grpc_stats_data* data) {
  std::vector<std::string> parts;
  parts.push_back("{");
  for (size_t i = 0; i < GRPC_STATS_COUNTER_COUNT; i++) {
    parts.push_back(absl::StrFormat(
        "\"%s\": %" PRIdPTR, grpc_stats_counter_name[i], data->counters[i]));
  }
  for (size_t i = 0; i < GRPC_STATS_HISTOGRAM_COUNT; i++) {
    parts.push_back(absl::StrFormat("\"%s\": [", grpc_stats_histogram_name[i]));
    for (int j = 0; j < grpc_stats_histo_buckets[i]; j++) {
      parts.push_back(
          absl::StrFormat("%s%" PRIdPTR, j == 0 ? "" : ",",
                          data->histograms[grpc_stats_histo_start[i] + j]));
    }
    parts.push_back(
        absl::StrFormat("], \"%s_bkt\": [", grpc_stats_histogram_name[i]));
    for (int j = 0; j < grpc_stats_histo_buckets[i]; j++) {
      parts.push_back(absl::StrFormat(
          "%s%d", j == 0 ? "" : ",", grpc_stats_histo_bucket_boundaries[i][j]));
    }
    parts.push_back("]");
  }
  parts.push_back("}");
  return absl::StrJoin(parts, "");
}
