// Copyright 2023 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/backoff/random_early_detection.h"

namespace grpc_core {

bool RandomEarlyDetection::Reject(uint64_t size) {
  if (size <= soft_limit_) return false;
  if (size < hard_limit_) {
    return absl::Bernoulli(bitgen_,
                           static_cast<double>(size - soft_limit_) /
                               static_cast<double>(hard_limit_ - soft_limit_));
  }
  return true;
}

}  // namespace grpc_core
