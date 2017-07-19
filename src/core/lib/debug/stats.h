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

#ifndef GRPC_CORE_LIB_DEBUG_STATS_H
#define GRPC_CORE_LIB_DEBUG_STATS_H

#include <grpc/support/atm.h>
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/iomgr/exec_ctx.h"

typedef struct grpc_stats_data {
  gpr_atm counters[GRPC_STATS_COUNTER_COUNT];
} grpc_stats_data;

extern grpc_stats_data *grpc_stats_per_cpu_storage;

#define GRPC_THREAD_STATS_DATA(exec_ctx) \
  (&grpc_stats_per_cpu_storage[(exec_ctx)->starting_cpu])

#define GRPC_STATS_INC_COUNTER(exec_ctx, ctr) \
  (gpr_atm_no_barrier_fetch_add(              \
      &GRPC_THREAD_STATS_DATA((exec_ctx))->counters[(ctr)], 1))

void grpc_stats_init(void);
void grpc_stats_shutdown(void);
void grpc_stats_collect(grpc_stats_data *output);
char *grpc_stats_data_as_json(const grpc_stats_data *data);

#endif
