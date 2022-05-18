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

#include "src/cpp/util/core_stats.h"

#include <grpc/support/log.h>

using grpc::core::Bucket;
using grpc::core::Histogram;
using grpc::core::Metric;
using grpc::core::Stats;

namespace grpc {

void CoreStatsToProto(const grpc_stats_data& core, Stats* proto) {
  for (int i = 0; i < GRPC_STATS_COUNTER_COUNT; i++) {
    Metric* m = proto->add_metrics();
    m->set_name(grpc_stats_counter_name[i]);
    m->set_count(core.counters[i]);
  }
  for (int i = 0; i < GRPC_STATS_HISTOGRAM_COUNT; i++) {
    Metric* m = proto->add_metrics();
    m->set_name(grpc_stats_histogram_name[i]);
    Histogram* h = m->mutable_histogram();
    for (int j = 0; j < grpc_stats_histo_buckets[i]; j++) {
      Bucket* b = h->add_buckets();
      b->set_start(grpc_stats_histo_bucket_boundaries[i][j]);
      b->set_count(core.histograms[grpc_stats_histo_start[i] + j]);
    }
  }
}

void ProtoToCoreStats(const grpc::core::Stats& proto, grpc_stats_data* core) {
  memset(core, 0, sizeof(*core));
  for (const auto& m : proto.metrics()) {
    switch (m.value_case()) {
      case Metric::VALUE_NOT_SET:
        break;
      case Metric::kCount:
        for (int i = 0; i < GRPC_STATS_COUNTER_COUNT; i++) {
          if (m.name() == grpc_stats_counter_name[i]) {
            core->counters[i] = m.count();
            break;
          }
        }
        break;
      case Metric::kHistogram:
        for (int i = 0; i < GRPC_STATS_HISTOGRAM_COUNT; i++) {
          if (m.name() == grpc_stats_histogram_name[i]) {
            const auto& h = m.histogram();
            bool valid = true;
            if (grpc_stats_histo_buckets[i] != h.buckets_size()) valid = false;
            for (int j = 0; valid && j < h.buckets_size(); j++) {
              if (grpc_stats_histo_bucket_boundaries[i][j] !=
                  h.buckets(j).start()) {
                valid = false;
              }
            }
            if (!valid) {
              gpr_log(GPR_ERROR,
                      "Found histogram %s but shape is different from proto",
                      m.name().c_str());
            }
            for (int j = 0; valid && j < h.buckets_size(); j++) {
              core->histograms[grpc_stats_histo_start[i] + j] =
                  h.buckets(j).count();
            }
          }
        }
        break;
    }
  }
}

}  // namespace grpc
