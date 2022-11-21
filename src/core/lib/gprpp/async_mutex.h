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

#ifndef GRPC_CORE_LIB_GPRPP_ASYNC_MUTEX_H
#define GRPC_CORE_LIB_GPRPP_ASYNC_MUTEX_H

#include <atomic>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

class AsyncMutex {
 public:
  explicit AsyncMutex(
      grpc_event_engine::experimental::EventEngine* event_engine);
  ~AsyncMutex();

  AsyncMutex(const AsyncMutex&) = delete;
  AsyncMutex& operator=(const AsyncMutex&) = delete;

  template <bool kAllowRunInline = false, bool kLowPriority = false>
  struct BasicEnqueueOptions {
    BasicEnqueueOptions<true, kLowPriority> AllowRunInline() const {
      return BasicEnqueueOptions<true, kLowPriority>{};
    }
    BasicEnqueueOptions<kAllowRunInline, true> LowPriority() const {
      return BasicEnqueueOptions<kAllowRunInline, true>{};
    }
  };

  using EnqueueOptions = BasicEnqueueOptions<>;

  void Enqueue(absl::AnyInvocable<void()> callback) {
    Enqueue(std::move(callback), EnqueueOptions());
  }

  template <bool kAllowRunInline, bool kLowPriority>
  void Enqueue(absl::AnyInvocable<void()> callback,
               BasicEnqueueOptions<kAllowRunInline, kLowPriority> options) {
    ReleasableMutexLock lock(&mu_);
    if (kLowPriority) {
      owner_->EnqueueLowPriority(std::move(callback));
      return;
    }
    if (kAllowRunInline) {
      if (owner_ == nullptr) {
        InlineOwner owner(this);
        lock.Release();
        callback();
        owner.Shutdown(this);
        return;
      }
    } else if (owner_ == nullptr) {
      StartOffloadOwner();
    }
    owner_->EnqueueHighPriority(std::move(callback));
  }

 private:
  class Owner {
   public:
    explicit Owner(AsyncMutex* mutex)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex->mu_) {
      GPR_DEBUG_ASSERT(mutex->owner_ == nullptr);
      mutex->owner_ = this;
    }
    explicit Owner(AsyncMutex* mutex,
                   std::vector<absl::AnyInvocable<void()>> low_priority_queue,
                   std::vector<absl::AnyInvocable<void()>> high_priority_queue)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex->mu_)
        : low_priority_queue_(std::move(low_priority_queue)),
          high_priority_queue_(std::move(high_priority_queue)) {
      GPR_DEBUG_ASSERT(mutex->owner_ == nullptr);
      mutex->owner_ = this;
    }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;

    void EnqueueLowPriority(absl::AnyInvocable<void()> callback) {
      low_priority_queue_.emplace_back(std::move(callback));
    }

    void EnqueueHighPriority(absl::AnyInvocable<void()> callback) {
      high_priority_queue_.emplace_back(std::move(callback));
    }

    void Shutdown(AsyncMutex* mutex) ABSL_LOCKS_EXCLUDED(mutex->mu_);

   protected:
    bool HasHighPriorityQueue() const { return !high_priority_queue_.empty(); }
    std::vector<absl::AnyInvocable<void()>> TakeLowPriorityQueue() {
      return std::move(low_priority_queue_);
    }
    std::vector<absl::AnyInvocable<void()>> TakeHighPriorityQueue() {
      return std::move(high_priority_queue_);
    }

   private:
    std::vector<absl::AnyInvocable<void()>> low_priority_queue_;
    std::vector<absl::AnyInvocable<void()>> high_priority_queue_;
  };

  class InlineOwner final : public Owner {
   public:
    explicit InlineOwner(AsyncMutex* mutex) : Owner(mutex) {}

    void Shutdown(AsyncMutex* mutex) ABSL_LOCKS_EXCLUDED(mutex->mu_);
  };

  void StartOffloadOwner() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  Mutex mu_;
  Owner* owner_ ABSL_GUARDED_BY(mu_) = nullptr;
  grpc_event_engine::experimental::EventEngine* const event_engine_;
};

}  // namespace grpc_core

#endif
