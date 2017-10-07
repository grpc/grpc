/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/iomgr/time_averaged_stats.h"

void grpc_time_averaged_stats_init(grpc_time_averaged_stats* stats,
                                   double init_avg, double regress_weight,
                                   double persistence_factor) {
  stats->init_avg = init_avg;
  stats->regress_weight = regress_weight;
  stats->persistence_factor = persistence_factor;
  stats->batch_total_value = 0;
  stats->batch_num_samples = 0;
  stats->aggregate_total_weight = 0;
  stats->aggregate_weighted_avg = init_avg;
}

void grpc_time_averaged_stats_add_sample(grpc_time_averaged_stats* stats,
                                         double value) {
  stats->batch_total_value += value;
  ++stats->batch_num_samples;
}

double grpc_time_averaged_stats_update_average(
    grpc_time_averaged_stats* stats) {
  /* Start with the current batch: */
  double weighted_sum = stats->batch_total_value;
  double total_weight = stats->batch_num_samples;
  if (stats->regress_weight > 0) {
    /* Add in the regression towards init_avg_: */
    weighted_sum += stats->regress_weight * stats->init_avg;
    total_weight += stats->regress_weight;
  }
  if (stats->persistence_factor > 0) {
    /* Add in the persistence: */
    const double prev_sample_weight =
        stats->persistence_factor * stats->aggregate_total_weight;
    weighted_sum += prev_sample_weight * stats->aggregate_weighted_avg;
    total_weight += prev_sample_weight;
  }
  stats->aggregate_weighted_avg =
      (total_weight > 0) ? (weighted_sum / total_weight) : stats->init_avg;
  stats->aggregate_total_weight = total_weight;
  stats->batch_num_samples = 0;
  stats->batch_total_value = 0;
  return stats->aggregate_weighted_avg;
}
