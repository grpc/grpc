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

#include "test/cpp/microbenchmarks/helpers.h"

#include <string.h>

static grpc::internal::GrpcLibraryInitializer g_gli_initializer;
static LibraryInitializer* g_libraryInitializer;

LibraryInitializer::LibraryInitializer() {
  GPR_ASSERT(g_libraryInitializer == nullptr);
  g_libraryInitializer = this;

  g_gli_initializer.summon();
  init_lib_.init();
}

LibraryInitializer::~LibraryInitializer() {
  g_libraryInitializer = nullptr;
  init_lib_.shutdown();
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
        << grpc_stats_histo_percentile(
               &stats, static_cast<grpc_stats_histograms>(i), 50.0)
        << " " << grpc_stats_histogram_name[i] << "-99p:"
        << grpc_stats_histo_percentile(
               &stats, static_cast<grpc_stats_histograms>(i), 99.0);
  }
}
