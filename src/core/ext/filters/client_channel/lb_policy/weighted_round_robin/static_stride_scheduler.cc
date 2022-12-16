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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/lb_policy/weighted_round_robin/static_stride_scheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"

#include <grpc/support/log.h>

namespace grpc_core {

namespace {
constexpr uint16_t kMaxWeight = std::numeric_limits<uint16_t>::max();
}  // namespace

absl::optional<StaticStrideScheduler> StaticStrideScheduler::Make(
    absl::Span<const float> float_weights,
    absl::AnyInvocable<uint32_t()> next_sequence_func) {
  if (float_weights.empty()) return absl::nullopt;
  if (float_weights.size() == 1) return absl::nullopt;

  // TODO(b/190488683): should we normalize negative weights to 0?

  const size_t n = float_weights.size();
  size_t num_zero_weight_channels = 0;
  double sum = 0;
  float max = 0;
  for (const float weight : float_weights) {
    sum += weight;
    max = std::max(max, weight);
    if (weight == 0) {
      ++num_zero_weight_channels;
    }
  }

  if (num_zero_weight_channels == n) return absl::nullopt;

  // Mean of non-zero weights before scaling to `kMaxWeight`.
  const double unscaled_mean =
      sum / static_cast<double>(n - num_zero_weight_channels);

  // Scale weights such that the largest is equal to `kMaxWeight`. This should
  // be accurate enough once we convert to an integer. Quantisation errors won't
  // be measurable on borg.
  // TODO(b/190488683): it may be more stable over updates if we try to keep
  // `scaling_factor` consistent, and only change it when we can't accurately
  // represent the new weights.
  const double scaling_factor = kMaxWeight / max;
  const uint16_t mean = std::lround(scaling_factor * unscaled_mean);

  std::vector<uint16_t> weights;
  weights.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    weights.push_back(float_weights[i] == 0
                          ? mean
                          : std::lround(float_weights[i] * scaling_factor));
  }

  GPR_ASSERT(weights.size() == float_weights.size());
  return StaticStrideScheduler{std::move(weights),
                               std::move(next_sequence_func)};
}

StaticStrideScheduler::StaticStrideScheduler(
    std::vector<uint16_t> weights,
    absl::AnyInvocable<uint32_t()> next_sequence_func)
    : next_sequence_func_(std::move(next_sequence_func)),
      weights_(std::move(weights)) {
  GPR_ASSERT(next_sequence_func_ != nullptr);
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
