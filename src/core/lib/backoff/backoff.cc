/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/backoff/backoff.h"

#include <algorithm>

#include "src/core/lib/gpr/random.h"

namespace grpc_core {

BackOff::BackOff(const Options& options)
    : options_(options),
      rng_state_(static_cast<uint32_t>(gpr_now(GPR_CLOCK_REALTIME).tv_nsec)) {
  Reset();
}

grpc_millis BackOff::NextAttemptTime() {
  if (initial_) {
    initial_ = false;
    return current_backoff_ + grpc_core::ExecCtx::Get()->Now();
  }
  current_backoff_ = static_cast<grpc_millis>(
      std::min(current_backoff_ * options_.multiplier(),
               static_cast<double>(options_.max_backoff())));
  const double jitter = gpr_generate_uniform_random_number_between(
      &rng_state_, -options_.jitter() * current_backoff_,
      options_.jitter() * current_backoff_);
  const grpc_millis next_timeout =
      static_cast<grpc_millis>(current_backoff_ + jitter);
  return next_timeout + grpc_core::ExecCtx::Get()->Now();
}

void BackOff::Reset() {
  current_backoff_ = options_.initial_backoff();
  initial_ = true;
}

void BackOff::SetRandomSeed(uint32_t seed) { rng_state_ = seed; }

}  // namespace grpc_core
