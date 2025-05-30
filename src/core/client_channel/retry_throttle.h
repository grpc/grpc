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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_RETRY_THROTTLE_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_RETRY_THROTTLE_H

#include <stdint.h>

#include <atomic>

#include "src/core/filter/blackboard.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {
namespace internal {

/// Tracks retry throttling data for an individual server name.
class RetryThrottler final : public Blackboard::Entry {
 public:
  static RefCountedPtr<RetryThrottler> Create(
      uintptr_t max_milli_tokens, uintptr_t milli_token_ratio,
      RefCountedPtr<RetryThrottler> previous);

  static UniqueTypeName Type();

  // Do not instantiate directly -- use Create() instead.
  RetryThrottler(uintptr_t max_milli_tokens, uintptr_t milli_token_ratio,
                 uintptr_t milli_tokens);
  ~RetryThrottler() override;

  /// Records a failure.  Returns true if it's okay to send a retry.
  bool RecordFailure();

  /// Records a success.
  void RecordSuccess();

  // Exposed for testing purposes only.
  uintptr_t max_milli_tokens() const { return max_milli_tokens_; }
  uintptr_t milli_token_ratio() const { return milli_token_ratio_; }
  intptr_t milli_tokens() const {
    return milli_tokens_.load(std::memory_order_relaxed);
  }

 private:
  void SetReplacement(RefCountedPtr<RetryThrottler> replacement);

  void GetReplacementThrottleDataIfNeeded(RetryThrottler** throttle_data);

  const uintptr_t max_milli_tokens_;
  const uintptr_t milli_token_ratio_;
  std::atomic<intptr_t> milli_tokens_;
  // A pointer to the replacement for this RetryThrottler entry.
  // If non-nullptr, then this entry is stale and must not be used.
  // We hold a reference to the replacement.
  std::atomic<RetryThrottler*> replacement_{nullptr};
};

}  // namespace internal
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_RETRY_THROTTLE_H
