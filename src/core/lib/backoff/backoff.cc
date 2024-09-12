//
//
// Copyright 2016 gRPC authors.
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

#include "src/core/lib/backoff/backoff.h"

#include <algorithm>

namespace grpc_core {

BackOff::BackOff(const Options& options) : options_(options) { Reset(); }

Duration BackOff::NextAttemptDelay() {
  if (initial_) {
    initial_ = false;
  } else {
    current_backoff_ = std::min(current_backoff_ * options_.multiplier(),
                                options_.max_backoff());
  }
  const double jitter =
      absl::Uniform(rand_gen_, 1 - options_.jitter(), 1 + options_.jitter());
  return current_backoff_ * jitter;
}

void BackOff::Reset() {
  current_backoff_ = options_.initial_backoff();
  initial_ = true;
}

}  // namespace grpc_core
