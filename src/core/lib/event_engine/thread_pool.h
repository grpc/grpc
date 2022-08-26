/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
#define GRPC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <queue>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"

#include "src/core/lib/event_engine/forkable.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

class ThreadPool final : public grpc_event_engine::experimental::Forkable {
 public:
  explicit ThreadPool(int reserve_threads);
  ~ThreadPool() override;

  void Add(absl::AnyInvocable<void()> callback);

  // Forkable
  void PrepareFork() override;
  void PostforkParent() override;
  void PostforkChild() override;

 private:
  class Queue {
   public:
    explicit Queue(int reserve_threads) : reserve_threads_(reserve_threads) {}
    bool Step();
    void SetShutdown() { SetState(State::kShutdown); }
    void SetForking() { SetState(State::kForking); }
    bool Add(absl::AnyInvocable<void()> callback);

   private:
    enum class State { kRunning, kShutdown, kForking };

    void SetState(State state);

    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_;
    std::queue<absl::AnyInvocable<void()>> callbacks_ ABSL_GUARDED_BY(mu_);
    int threads_waiting_ ABSL_GUARDED_BY(mu_) = 0;
    const int reserve_threads_;
    State state_ ABSL_GUARDED_BY(mu_) = State::kRunning;
  };

  class ThreadCount {
   public:
    void Add();
    void Remove();
    void Quiesce();

   private:
    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_;
    int threads_ ABSL_GUARDED_BY(mu_) = 0;
  };

  static void ThreadFunc(std::weak_ptr<Queue> weak_queue,
                         std::shared_ptr<ThreadCount> thread_count);
  static void StartThread(std::weak_ptr<Queue> weak_queue,
                          std::shared_ptr<ThreadCount> thread_count);

  const int reserve_threads_;
  std::shared_ptr<Queue> queue_ = std::make_shared<Queue>(reserve_threads_);
  const std::shared_ptr<ThreadCount> thread_count_ =
      std::make_shared<ThreadCount>();
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
