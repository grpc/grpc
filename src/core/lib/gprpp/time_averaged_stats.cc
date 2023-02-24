//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/time_averaged_stats.h"

namespace grpc_core {

TimeAveragedStats::TimeAveragedStats(double init_avg, double regress_weight,
                                     double persistence_factor)
    : init_avg_(init_avg),
      regress_weight_(regress_weight),
      persistence_factor_(persistence_factor) {}

void TimeAveragedStats::AddSample(double value) {
  batch_total_value_ += value;
  ++batch_num_samples_;
}

double TimeAveragedStats::UpdateAverage() {
  // Start with the current batch:
  double weighted_sum = batch_total_value_;
  double total_weight = batch_num_samples_;
  if (regress_weight_ > 0) {
    // Add in the regression towards init_avg_:
    weighted_sum += regress_weight_ * init_avg_;
    total_weight += regress_weight_;
  }
  if (persistence_factor_ > 0) {
    // Add in the persistence:
    const double prev_sample_weight =
        persistence_factor_ * aggregate_total_weight_;
    weighted_sum += prev_sample_weight * aggregate_weighted_avg_;
    total_weight += prev_sample_weight;
  }
  aggregate_weighted_avg_ =
      (total_weight > 0) ? (weighted_sum / total_weight) : init_avg_;
  aggregate_total_weight_ = total_weight;
  batch_num_samples_ = 0;
  batch_total_value_ = 0;
  return aggregate_weighted_avg_;
}

}  // namespace grpc_core
