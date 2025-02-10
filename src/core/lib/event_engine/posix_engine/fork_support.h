// Copyright 2024 The gRPC Authors
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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FORK_SUPPORT_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FORK_SUPPORT_H

#include <unordered_map>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "src/core/util/sync.h"

namespace grpc_event_engine {
namespace experimental {

class ForkSupport;

class ForkSubscription {
 public:
  ForkSubscription(ForkSupport* fork_support, int key)
      : fork_support_(fork_support), key_(key) {}
  ForkSubscription(const ForkSubscription& other) = delete;
  ForkSubscription(ForkSubscription&& other) noexcept
      : fork_support_(other.fork_support_), key_(other.key_) {
    other.fork_support_ = nullptr;
    other.key_ = 0;
  }
  ~ForkSubscription();

 private:
  ForkSupport* fork_support_;
  int key_;
};

class ForkSupport {
 public:
  enum class ForkEvent { kPreFork, kPostFork };

  ForkSubscription Subscribe(absl::AnyInvocable<void(ForkEvent)> listener) {
    grpc_core::MutexLock lock(&mu_);
    int key = next_key_++;
    listeners_.emplace(key, std::move(listener));
    return ForkSubscription(this, key);
  }

  void PrepareFork() { Signal(ForkEvent::kPreFork); }
  void PostFork() { Signal(ForkEvent::kPostFork); }

 private:
  friend class ForkSubscription;

  void Signal(ForkEvent event) {
    grpc_core::MutexLock lock(&mu_);
    for (auto& key_listener : listeners_) {
      key_listener.second(event);
    }
  }

  void Unsubscribe(int key) {
    grpc_core::MutexLock lock(&mu_);
    listeners_.erase(key);
  }

  grpc_core::Mutex mu_;
  std::unordered_map<int, absl::AnyInvocable<void(ForkEvent)>> listeners_
      ABSL_GUARDED_BY(&mu_);
  int next_key_ ABSL_GUARDED_BY(&mu_) = 1;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FORK_SUPPORT_H