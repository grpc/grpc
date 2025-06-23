//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/client_channel/retry_throttle.h"

#include <atomic>
#include <cstdint>
#include <limits>

#include "src/core/util/useful.h"

namespace grpc_core {
namespace internal {

namespace {

template <typename T>
T ClampedAdd(std::atomic<T>& value, T delta, T min, T max) {
  T prev_value = value.load(std::memory_order_relaxed);
  T new_value;
  do {
    new_value = Clamp(SaturatingAdd(prev_value, delta), min, max);
  } while (!value.compare_exchange_weak(prev_value, new_value,
                                        std::memory_order_relaxed));
  return new_value;
}

}  // namespace

//
// RetryThrottler
//

RefCountedPtr<RetryThrottler> RetryThrottler::Create(
    uintptr_t max_milli_tokens, uintptr_t milli_token_ratio,
    RefCountedPtr<RetryThrottler> previous) {
  if (previous != nullptr && previous->max_milli_tokens_ == max_milli_tokens &&
      previous->milli_token_ratio_ == milli_token_ratio) {
    return previous;
  }
  // previous is null or has different parameters.  Create a new one.
  uintptr_t initial_milli_tokens = max_milli_tokens;
  // If there was a pre-existing entry for this server name, initialize
  // the token count by scaling proportionately to the old data.  This
  // ensures that if we're already throttling retries on the old scale,
  // we will start out doing the same thing on the new one.
  if (previous != nullptr) {
    double token_fraction = static_cast<double>(previous->milli_tokens_) /
                            static_cast<double>(previous->max_milli_tokens_);
    initial_milli_tokens =
        static_cast<uintptr_t>(token_fraction * max_milli_tokens);
  }
  auto throttle_data = MakeRefCounted<RetryThrottler>(
      max_milli_tokens, milli_token_ratio, initial_milli_tokens);
  if (previous != nullptr) previous->SetReplacement(throttle_data);
  return throttle_data;
}

UniqueTypeName RetryThrottler::Type() {
  static UniqueTypeName::Factory factory("retry_throttle");
  return factory.Create();
}

RetryThrottler::RetryThrottler(uintptr_t max_milli_tokens,
                               uintptr_t milli_token_ratio,
                               uintptr_t milli_tokens)
    : max_milli_tokens_(max_milli_tokens),
      milli_token_ratio_(milli_token_ratio),
      milli_tokens_(milli_tokens) {}

RetryThrottler::~RetryThrottler() {
  RetryThrottler* replacement = replacement_.load(std::memory_order_acquire);
  if (replacement != nullptr) {
    replacement->Unref();
  }
}

void RetryThrottler::SetReplacement(RefCountedPtr<RetryThrottler> replacement) {
  replacement_.store(replacement.release(), std::memory_order_release);
}

void RetryThrottler::GetReplacementThrottleDataIfNeeded(
    RetryThrottler** throttle_data) {
  while (true) {
    RetryThrottler* new_throttle_data =
        (*throttle_data)->replacement_.load(std::memory_order_acquire);
    if (new_throttle_data == nullptr) return;
    *throttle_data = new_throttle_data;
  }
}

bool RetryThrottler::RecordFailure() {
  // First, check if we are stale and need to be replaced.
  RetryThrottler* throttle_data = this;
  GetReplacementThrottleDataIfNeeded(&throttle_data);
  // We decrement milli_tokens by 1000 (1 token) for each failure.
  const uintptr_t new_value = ClampedAdd<intptr_t>(
      throttle_data->milli_tokens_, -1000, 0,
      std::min<uintptr_t>(throttle_data->max_milli_tokens_,
                          std::numeric_limits<intptr_t>::max()));
  // Retries are allowed as long as the new value is above the threshold
  // (max_milli_tokens / 2).
  return new_value > throttle_data->max_milli_tokens_ / 2;
}

void RetryThrottler::RecordSuccess() {
  // First, check if we are stale and need to be replaced.
  RetryThrottler* throttle_data = this;
  GetReplacementThrottleDataIfNeeded(&throttle_data);
  // We increment milli_tokens by milli_token_ratio for each success.
  ClampedAdd<intptr_t>(
      throttle_data->milli_tokens_, throttle_data->milli_token_ratio_, 0,
      std::max<intptr_t>(
          0, std::min<uintptr_t>(throttle_data->max_milli_tokens_,
                                 std::numeric_limits<intptr_t>::max())));
}

}  // namespace internal
}  // namespace grpc_core
