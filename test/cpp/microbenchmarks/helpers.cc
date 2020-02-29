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

#include <string.h>

#include "test/cpp/microbenchmarks/helpers.h"

static grpc::internal::GrpcLibraryInitializer g_gli_initializer;
static LibraryInitializer* g_libraryInitializer;

LibraryInitializer::LibraryInitializer() {
  GPR_ASSERT(g_libraryInitializer == nullptr);
  g_libraryInitializer = this;

  g_gli_initializer.summon();
#ifdef GPR_LOW_LEVEL_COUNTERS
  grpc_memory_counters_init();
#endif
  init_lib_.init();
  rq_ = grpc_resource_quota_create("bm");
}

LibraryInitializer::~LibraryInitializer() {
  g_libraryInitializer = nullptr;
  init_lib_.shutdown();
  grpc_resource_quota_unref(rq_);
}

LibraryInitializer& LibraryInitializer::get() {
  GPR_ASSERT(g_libraryInitializer != nullptr);
  return *g_libraryInitializer;
}

void TrackCounters::Finish(benchmark::State& state) {
  std::ostringstream out;
  for (const auto& l : labels_) {
    out << l << ' ';
  }
  AddToLabel(out, state);
  std::string label = out.str();
  if (label.length() && label[0] == ' ') {
    label = label.substr(1);
  }
  state.SetLabel(label.c_str());
}

void TrackCounters::AddLabel(const std::string& label) {
  labels_.push_back(label);
}

void TrackCounters::AddToLabel(std::ostream& out, benchmark::State& state) {
  // Use the parameters to avoid unused-parameter warnings depending on the
  // #define's present
  (void)out;
  (void)state;
#ifdef GRPC_COLLECT_STATS
  grpc_stats_data stats_end;
  grpc_stats_collect(&stats_end);
  grpc_stats_data stats;
  grpc_stats_diff(&stats_end, &stats_begin_, &stats);
  for (int i = 0; i < GRPC_STATS_COUNTER_COUNT; i++) {
    out << " " << grpc_stats_counter_name[i] << "/iter:"
        << (static_cast<double>(stats.counters[i]) /
            static_cast<double>(state.iterations()));
  }
  for (int i = 0; i < GRPC_STATS_HISTOGRAM_COUNT; i++) {
    out << " " << grpc_stats_histogram_name[i] << "-median:"
        << grpc_stats_histo_percentile(&stats, (grpc_stats_histograms)i, 50.0)
        << " " << grpc_stats_histogram_name[i] << "-99p:"
        << grpc_stats_histo_percentile(&stats, (grpc_stats_histograms)i, 99.0);
  }
#endif
#ifdef GPR_LOW_LEVEL_COUNTERS
  grpc_memory_counters counters_at_end = grpc_memory_counters_snapshot();
  out << " locks/iter:"
      << ((double)(gpr_atm_no_barrier_load(&gpr_mu_locks) -
                   mu_locks_at_start_) /
          (double)state.iterations())
      << " atm_cas/iter:"
      << ((double)(gpr_atm_no_barrier_load(&gpr_counter_atm_cas) -
                   atm_cas_at_start_) /
          (double)state.iterations())
      << " atm_add/iter:"
      << ((double)(gpr_atm_no_barrier_load(&gpr_counter_atm_add) -
                   atm_add_at_start_) /
          (double)state.iterations())
      << " nows/iter:"
      << ((double)(gpr_atm_no_barrier_load(&gpr_now_call_count) -
                   now_calls_at_start_) /
          (double)state.iterations())
      << " allocs/iter:"
      << ((double)(counters_at_end.total_allocs_absolute -
                   counters_at_start_.total_allocs_absolute) /
          (double)state.iterations());
#endif
}
