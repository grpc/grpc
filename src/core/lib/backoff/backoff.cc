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

#include "src/core/lib/backoff/backoff.h"

#include <algorithm>
#include <cstdlib>

#include <grpc/support/useful.h>

namespace grpc_core {

namespace {
static double generate_uniform_random_number_between(double a, double b) {
  if (a == b) return a;
  if (a > b) GPR_SWAP(double, a, b);  // make sure a < b
  const double range = b - a;
  const double zero_to_one_rand = rand() / (double)RAND_MAX;
  return a + zero_to_one_rand * range;
}
}  // namespace

Backoff::Backoff(const Options& options) : options_(options) {
  seed = (unsigned int)gpr_now(GPR_CLOCK_REALTIME).tv_nsec;
}

Backoff::Result Backoff::Begin(grpc_exec_ctx* exec_ctx) {
  current_backoff_ = options_.initial_backoff();
  const grpc_millis initial_timeout =
      std::max(options_.initial_backoff(), options_.min_connect_timeout());
  const grpc_millis now = grpc_exec_ctx_now(exec_ctx);
  return Backoff::Result{now + initial_timeout, now + current_backoff_};
}

Backoff::Result Backoff::Step(grpc_exec_ctx* exec_ctx) {
  current_backoff_ =
      (grpc_millis)(std::min(current_backoff_ * options_.multiplier(),
                             (double)options_.max_backoff()));
  const double jitter = generate_uniform_random_number_between(
      -options_.jitter() * current_backoff_,
      options_.jitter() * current_backoff_);
  const grpc_millis current_timeout = std::max(
      (grpc_millis)(current_backoff_ + jitter), options_.min_connect_timeout());
  const grpc_millis next_timeout = std::min(
      (grpc_millis)(current_backoff_ + jitter), options_.max_backoff());
  const grpc_millis now = grpc_exec_ctx_now(exec_ctx);
  return Backoff::Result{now + current_timeout, now + next_timeout};
}

void Backoff::Reset() { current_backoff_ = options_.initial_backoff(); }

void Backoff::SetRandomSeed(uint32_t seed) { srand(seed); }

}  // namespace grpc_core
