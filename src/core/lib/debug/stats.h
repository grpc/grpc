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

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <string>

#include <grpc/support/atm.h>

#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/iomgr/exec_ctx.h"

typedef struct grpc_stats_data {
  gpr_atm counters[GRPC_STATS_COUNTER_COUNT];
  gpr_atm histograms[GRPC_STATS_HISTOGRAM_BUCKETS];
} grpc_stats_data;

namespace grpc_core {
struct Stats {
  size_t num_cores;
  grpc_stats_data per_cpu[0];
};
extern Stats* const g_stats_data;
}  // namespace grpc_core

#define GRPC_THREAD_STATS_DATA() \
  (&::grpc_core::g_stats_data    \
        ->per_cpu[grpc_core::ExecCtx::Get()->starting_cpu()])

/* Only collect stats if GRPC_COLLECT_STATS is defined or it is a debug build.
 */
#if defined(GRPC_COLLECT_STATS) || !defined(NDEBUG)
#define GRPC_STATS_INC_COUNTER(ctr) \
  (gpr_atm_no_barrier_fetch_add(&GRPC_THREAD_STATS_DATA()->counters[(ctr)], 1))

#define GRPC_STATS_INC_HISTOGRAM(histogram, index)                             \
  (gpr_atm_no_barrier_fetch_add(                                               \
      &GRPC_THREAD_STATS_DATA()->histograms[histogram##_FIRST_SLOT + (index)], \
      1))
#else /* defined(GRPC_COLLECT_STATS) || !defined(NDEBUG) */
#define GRPC_STATS_INC_COUNTER(ctr)
#define GRPC_STATS_INC_HISTOGRAM(histogram, index)
#endif /* defined(GRPC_COLLECT_STATS) || !defined(NDEBUG) */

GRPC_DEPRECATED("function is no longer needed")
inline void grpc_stats_init(void) {}
void grpc_stats_collect(grpc_stats_data* output);
// c = b-a
void grpc_stats_diff(const grpc_stats_data* b, const grpc_stats_data* a,
                     grpc_stats_data* c);
std::string grpc_stats_data_as_json(const grpc_stats_data* data);
int grpc_stats_histo_find_bucket_slow(int value, const int* table,
                                      int table_size);
double grpc_stats_histo_percentile(const grpc_stats_data* stats,
                                   grpc_stats_histograms histogram,
                                   double percentile);
size_t grpc_stats_histo_count(const grpc_stats_data* stats,
                              grpc_stats_histograms histogram);

#endif  // GRPC_CORE_LIB_DEBUG_STATS_H
