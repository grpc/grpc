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

#include "src/core/lib/support/string.h"

grpc_stats_data *grpc_stats_per_cpu_storage = NULL;
static size_t g_num_cores;

void grpc_stats_init(void) {
  g_num_cores = GPR_MAX(1, gpr_cpu_num_cores());
  grpc_stats_per_cpu_storage =
      gpr_zalloc(sizeof(grpc_stats_data) * g_num_cores);
}

void grpc_stats_shutdown(void) { gpr_free(grpc_stats_per_cpu_storage); }

void grpc_stats_collect(grpc_stats_data *output) {
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

int grpc_stats_histo_find_bucket_slow(grpc_exec_ctx *exec_ctx, double value,
                                      const double *table, int table_size) {
  GRPC_STATS_INC_HISTOGRAM_SLOW_LOOKUPS(exec_ctx);
  if (value < 0.0) return 0;
  if (value >= table[table_size - 1]) return table_size - 1;
  int a = 0;
  int b = table_size - 1;
  while (a < b) {
    int c = a + ((b - a) / 2);
    if (value < table[c]) {
      b = c - 1;
    } else if (value > table[c]) {
      a = c + 1;
    } else {
      return c;
    }
  }
  return a;
}

char *grpc_stats_data_as_json(const grpc_stats_data *data) {
  gpr_strvec v;
  char *tmp;
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
      gpr_asprintf(&tmp, "%s%lf", j == 0 ? "" : ",",
                   grpc_stats_histo_bucket_boundaries[i][j]);
      gpr_strvec_add(&v, tmp);
    }
    gpr_strvec_add(&v, gpr_strdup("]"));
    is_first = false;
  }
  gpr_strvec_add(&v, gpr_strdup("}"));
  tmp = gpr_strvec_flatten(&v, NULL);
  gpr_strvec_destroy(&v);
  return tmp;
}
