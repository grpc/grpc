//
// Copyright 2022 gRPC authors.
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

#include "src/core/load_balancing/weighted_round_robin/static_stride_scheduler.h"

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"

namespace grpc_core {

namespace {

constexpr uint16_t kMaxWeight = std::numeric_limits<uint16_t>::max();

// Assuming the mean of all known weights is M, StaticStrideScheduler will cap
// from above all known weights that are bigger than M*kMaxRatio (to
// M*kMaxRatio).
//
// This is done to limit the number of rounds for picks.
constexpr double kMaxRatio = 10;

// Assuming the mean of all known weights is M, StaticStrideScheduler will cap
// from below all known weights to M*kMinRatio.
//
// This is done as a performance optimization for edge cases when channels with
// large weights are non-accepting (and thus WeightedRoundRobin will retry
// picking them over and over again), and there are also channels with near-zero
// weights that are possibly accepting. In this case, without kMinRatio, it
// would potentially require WeightedRoundRobin to perform thousands of picks
// until it gets a single channel with near-zero weight. This was a part of what
// happened in b/276292666.
//
// The current value of 0.01 was chosen without any experimenting. It should
// ensure that WeightedRoundRobin doesn't do much more than an order of 100
// picks of non-accepting channels with high weights in such corner cases. But
// it also makes WeightedRoundRobin to send slightly more requests to
// potentially very bad tasks (that would have near-zero weights) than zero.
// This is not necessarily a downside, though. Perhaps this is not a problem at
// all and we should increase this value (to 0.05 or 0.1) to save CPU cycles.
//
// Note that this class treats weights that are exactly equal to zero as unknown
// and thus needing to be replaced with M. This behavior itself makes sense
// (fresh channels without feedback information will get an average flow of
// requests). However, it follows from this that this class will replace weight
// = 0 with M, but weight = epsilon with M*kMinRatio, and this step function is
// logically faulty. A demonstration of this is that the function that computes
// weights in WeightedRoundRobin
// (http://google3/production/rpc/stubs/core/loadbalancing/weightedroundrobin.cc;l=324-325;rcl=514986476)
// will cap some epsilon values to zero. There should be a clear distinction
// between "task is new, weight is unknown" and "task is unhealthy, weight is
// very low". A better solution would be to not mix "unknown" and "weight" into
// a single value but represent weights as std::optional<float> or, if memory
// usage is a concern, use NaN as the indicator of unknown weight.
constexpr double kMinRatio = 0.01;

}  // namespace

std::optional<StaticStrideScheduler> StaticStrideScheduler::Make(
    absl::Span<const float> float_weights,
    absl::AnyInvocable<uint32_t()> next_sequence_func) {
  if (float_weights.empty()) return std::nullopt;
  if (float_weights.size() == 1) return std::nullopt;

  // TODO(b/190488683): should we normalize negative weights to 0?

  const size_t n = float_weights.size();
  size_t num_zero_weight_channels = 0;
  double sum = 0;
  float unscaled_max = 0;
  for (const float weight : float_weights) {
    sum += weight;
    unscaled_max = std::max(unscaled_max, weight);
    if (weight == 0) {
      ++num_zero_weight_channels;
    }
  }

  if (num_zero_weight_channels == n) return std::nullopt;

  // Mean of non-zero weights before scaling to `kMaxWeight`.
  const double unscaled_mean =
      sum / static_cast<double>(n - num_zero_weight_channels);
  const double ratio = unscaled_max / unscaled_mean;

  // Adjust max value such that ratio does not exceed kMaxRatio. This should
  // ensure that we on average do at most kMaxRatio rounds for picks.
  if (ratio > kMaxRatio) {
    unscaled_max = kMaxRatio * unscaled_mean;
  }

  // Scale weights such that the largest is equal to `kMaxWeight`. This should
  // be accurate enough once we convert to an integer. Quantisation errors won't
  // be measurable on borg.
  // TODO(b/190488683): it may be more stable over updates if we try to keep
  // `scaling_factor` consistent, and only change it when we can't accurately
  // represent the new weights.
  const double scaling_factor = kMaxWeight / unscaled_max;

  // Note that since we cap the weights to stay within kMaxRatio, `mean` might
  // not match the actual mean of the values that end up in the scheduler.
  const uint16_t mean = std::lround(scaling_factor * unscaled_mean);

  // We compute weight_lower_bound and cap it to 1 from below so that in the
  // worst case we represent tiny weights as 1 but not as 0 (which would cause
  // an infinite loop as in b/276292666). This capping to 1 is probably only
  // useful in case someone misconfigures kMinRatio to be very small.
  //
  // NOMUTANTS -- We have tests for this expression, but they are not precise
  // enough to catch errors of plus/minus 1, what mutation testing does.
  const uint16_t weight_lower_bound =
      std::max(static_cast<uint16_t>(1),
               static_cast<uint16_t>(std::lround(mean * kMinRatio)));

  std::vector<uint16_t> weights;
  weights.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    if (float_weights[i] == 0) {  // Weight is unknown.
      weights.push_back(mean);
    } else {
      const double float_weight_capped_from_above =
          std::min(float_weights[i], unscaled_max);
      const uint16_t weight =
          std::lround(float_weight_capped_from_above * scaling_factor);
      weights.push_back(std::max(weight, weight_lower_bound));
    }
  }

  CHECK(weights.size() == float_weights.size());
  return StaticStrideScheduler{std::move(weights),
                               std::move(next_sequence_func)};
}

StaticStrideScheduler::StaticStrideScheduler(
    std::vector<uint16_t> weights,
    absl::AnyInvocable<uint32_t()> next_sequence_func)
    : next_sequence_func_(std::move(next_sequence_func)),
      weights_(std::move(weights)) {
  CHECK(next_sequence_func_ != nullptr);
}

size_t StaticStrideScheduler::Pick() const {
  while (true) {
    const uint32_t sequence = next_sequence_func_();

    // The sequence number is split in two: the lower %n gives the index of the
    // backend, and the rest gives the number of times we've iterated through
    // all backends. `generation` is used to deterministically decide whether
    // we pick or skip the backend on this iteration, in proportion to the
    // backend's weight.
    const uint64_t backend_index = sequence % weights_.size();
    const uint64_t generation = sequence / weights_.size();
    const uint64_t weight = weights_[backend_index];

    // We pick a backend `weight` times per `kMaxWeight` generations. The
    // multiply and modulus ~evenly spread out the picks for a given backend
    // between different generations. The offset by `backend_index` helps to
    // reduce the chance of multiple consecutive non-picks: if we have two
    // consecutive backends with an equal, say, 80% weight of the max, with no
    // offset we would see 1/5 generations that skipped both.
    // TODO(b/190488683): add test for offset efficacy.
    const uint16_t kOffset = kMaxWeight / 2;
    const uint16_t mod =
        (weight * generation + backend_index * kOffset) % kMaxWeight;

    if (mod < kMaxWeight - weight) {
      // Probability of skipping = 1 - mean(weights) / max(weights).
      // For a typical large-scale service using RR, max task utilization will
      // be ~100% when mean utilization is ~80%. So ~20% of picks will be
      // skipped.
      continue;
    }
    return backend_index;
  }
}

}  // namespace grpc_core
