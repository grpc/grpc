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
  }
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
  gpr_strvec_add(&v, gpr_strdup("}"));
  tmp = gpr_strvec_flatten(&v, NULL);
  gpr_strvec_destroy(&v);
  return tmp;
}
