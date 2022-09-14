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
  // Returns true if callbacks are being processed or are in queue
  bool IsBusy();

  // Forkable
  void PrepareFork() override;
  void PostforkParent() override;
  void PostforkChild() override;

 private:
  class Queue {
   public:
    explicit Queue(int reserve_threads) : reserve_threads_(reserve_threads) {}
    bool Step();
    void SetShutdown() { SetState(QueueState::kShutdown); }
    void SetForking() { SetState(QueueState::kForking); }
    // Add a callback to the queue.
    // Return true if we should also spin up a new thread.
    bool Add(absl::AnyInvocable<void()> callback);
    void Reset() { SetState(QueueState::kRunning); }
    // Returns true if callbacks are being processed or are in queue
    bool IsBusy(int expected_idle_threads);

   private:
    enum class QueueState { kRunning, kShutdown, kForking };

    void SetState(QueueState state);

    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_;
    std::queue<absl::AnyInvocable<void()>> callbacks_ ABSL_GUARDED_BY(mu_);
    int threads_waiting_ ABSL_GUARDED_BY(mu_) = 0;
    const int reserve_threads_;
    QueueState state_ ABSL_GUARDED_BY(mu_) = QueueState::kRunning;
  };

  class ThreadCount {
   public:
    void Add();
    void Remove();
    // Block until all threads have stopped.
    void Quiesce();
    int threads() {
      absl::MutexLock lock(&mu_);
      return threads_;
    }

   private:
    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_;
    int threads_ ABSL_GUARDED_BY(mu_) = 0;
  };

  struct ThreadState {
    explicit ThreadState(int reserve_threads) : queue(reserve_threads) {}
    Queue queue;
    ThreadCount thread_count;
  };

  using StatePtr = std::shared_ptr<ThreadState>;

  static void ThreadFunc(StatePtr state);
  static void StartThread(StatePtr state);
  void Postfork();

  const int reserve_threads_;
  const StatePtr state_ = std::make_shared<ThreadState>(reserve_threads_);
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
