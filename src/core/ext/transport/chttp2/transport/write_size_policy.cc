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

#include "src/core/ext/transport/chttp2/transport/write_size_policy.h"

namespace grpc_core {

namespace {
static constexpr size_t kMinTarget = 32 * 1024;
static constexpr const size_t kMaxTarget = 2 * 1024 * 1024;
static constexpr Duration kFastWrite = Duration::Milliseconds(100);
static constexpr Duration kSlowWrite = Duration::Seconds(1);
}  // namespace

size_t Chttp2WriteSizePolicy::WriteTargetSize() { return current_target_; }

void Chttp2WriteSizePolicy::BeginWrite(size_t size) {
  GPR_ASSERT(experiment_start_time_ == Timestamp::InfFuture());
  if (size < current_target_ * 7 / 10) {
    return;
  }
  experiment_start_time_ = Timestamp::Now();
}

void Chttp2WriteSizePolicy::EndWrite(bool success) {
  if (experiment_start_time_ == Timestamp::InfFuture()) {
    return;
  }
  const auto elapsed = Timestamp::Now() - experiment_start_time_;
  experiment_start_time_ = Timestamp::InfFuture();
  if (!success) return;
  if (elapsed < kFastWrite) {
    --state_;
    if (state_ == -2) {
      state_ = 0;
      current_target_ = std::min(current_target_ * 5 / 4, kMaxTarget);
    }
  } else if (elapsed > kSlowWrite) {
    ++state_;
    if (state_ == 2) {
      state_ = 0;
      current_target_ = std::max(current_target_ - kMinTarget, kMinTarget);
    }
  }
}

}  // namespace grpc_core
