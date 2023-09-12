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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_INTER_ACTIVITY_PIPE_H
#define GRPC_SRC_CORE_LIB_PROMISE_INTER_ACTIVITY_PIPE_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <array>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/types/optional.h"

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

template <typename T, uint8_t kQueueSize>
class InterActivityPipe {
 private:
  class Center : public RefCounted<Center, NonPolymorphicRefCount> {
   public:
    Poll<bool> Push(T& value) {
      ReleasableMutexLock lock(&mu_);
      if (closed_) return false;
      if (count_ == kQueueSize) {
        on_available_ = Activity::current()->MakeNonOwningWaker();
        return Pending{};
      }
      queue_[(first_ + count_) % kQueueSize] = std::move(value);
      ++count_;
      if (count_ == 1) {
        auto on_occupied = std::move(on_occupied_);
        lock.Release();
        on_occupied.Wakeup();
      }
      return true;
    }

    Poll<absl::optional<T>> Next() {
      ReleasableMutexLock lock(&mu_);
      if (count_ == 0) {
        if (closed_) return absl::nullopt;
        on_occupied_ = Activity::current()->MakeNonOwningWaker();
        return Pending{};
      }
      auto value = std::move(queue_[first_]);
      first_ = (first_ + 1) % kQueueSize;
      --count_;
      if (count_ == kQueueSize - 1) {
        auto on_available = std::move(on_available_);
        lock.Release();
        on_available.Wakeup();
      }
      return std::move(value);
    }

    void MarkClosed() {
      ReleasableMutexLock lock(&mu_);
      if (std::exchange(closed_, true)) return;
      auto on_occupied = std::move(on_occupied_);
      auto on_available = std::move(on_available_);
      lock.Release();
      on_occupied.Wakeup();
      on_available.Wakeup();
    }

   private:
    Mutex mu_;
    std::array<T, kQueueSize> queue_ ABSL_GUARDED_BY(mu_);
    bool closed_ ABSL_GUARDED_BY(mu_) = false;
    uint8_t first_ ABSL_GUARDED_BY(mu_) = 0;
    uint8_t count_ ABSL_GUARDED_BY(mu_) = 0;
    Waker on_occupied_ ABSL_GUARDED_BY(mu_);
    Waker on_available_ ABSL_GUARDED_BY(mu_);
  };
  RefCountedPtr<Center> center_{MakeRefCounted<Center>()};

 public:
  class Sender {
   public:
    explicit Sender(RefCountedPtr<Center> center)
        : center_(std::move(center)) {}
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;
    Sender(Sender&&) noexcept = default;
    Sender& operator=(Sender&&) noexcept = default;

    ~Sender() {
      if (center_ != nullptr) center_->MarkClosed();
    }

    auto Push(T value) {
      return [center = center_, value = std::move(value)]() mutable {
        return center->Push(value);
      };
    }

   private:
    RefCountedPtr<Center> center_;
  };

  class Receiver {
   public:
    explicit Receiver(RefCountedPtr<Center> center)
        : center_(std::move(center)) {}
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    Receiver(Receiver&&) noexcept = default;
    Receiver& operator=(Receiver&&) noexcept = default;

    ~Receiver() {
      if (center_ != nullptr) center_->MarkClosed();
    }

    auto Next() {
      return [center = center_]() { return center->Next(); };
    }

   private:
    RefCountedPtr<Center> center_;
  };

  Sender sender{center_};
  Receiver receiver{center_};
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_INTER_ACTIVITY_PIPE_H
