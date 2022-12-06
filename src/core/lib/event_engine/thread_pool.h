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

#include <stdint.h>

#include <atomic>
#include <memory>
#include <queue>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/cpu.h>

#include "src/core/lib/event_engine/executor/executor.h"
#include "src/core/lib/event_engine/forkable.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

class ThreadPool final : public Forkable, public Executor {
 public:
  ThreadPool();
  // Asserts Quiesce was called.
  ~ThreadPool() override;

  void Quiesce();

  // Run must not be called after Quiesce completes
  void Run(absl::AnyInvocable<void()> callback) override;
  void Run(EventEngine::Closure* closure) override;

  // Forkable
  // Ensures that the thread pool is empty before forking.
  void PrepareFork() override;
  void PostforkParent() override;
  void PostforkChild() override;

 private:
  class Queue {
   public:
    explicit Queue(unsigned reserve_threads)
        : reserve_threads_(reserve_threads) {}
    bool Step();
    void SetShutdown() { SetState(State::kShutdown); }
    void SetForking() { SetState(State::kForking); }
    // Add a callback to the queue.
    // Return true if we should also spin up a new thread.
    bool Add(absl::AnyInvocable<void()> callback);
    void Reset() { SetState(State::kRunning); }
    bool IsBacklogged();
    void SleepIfRunning();

   private:
    enum class State { kRunning, kShutdown, kForking };

    void SetState(State state);

    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_;
    std::queue<absl::AnyInvocable<void()>> callbacks_ ABSL_GUARDED_BY(mu_);
    unsigned threads_waiting_ ABSL_GUARDED_BY(mu_) = 0;
    const unsigned reserve_threads_;
    State state_ ABSL_GUARDED_BY(mu_) = State::kRunning;
  };

  class ThreadCount {
   public:
    void Add();
    void Remove();
    void BlockUntilThreadCount(int threads, const char* why);

   private:
    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_;
    int threads_ ABSL_GUARDED_BY(mu_) = 0;
  };

  struct State {
    explicit State(int reserve_threads) : queue(reserve_threads) {}
    Queue queue;
    ThreadCount thread_count;
    // After pool creation we use this to rate limit creation of threads to one
    // at a time.
    std::atomic<bool> currently_starting_one_thread{false};
    std::atomic<uint64_t> last_started_thread{0};
  };

  using StatePtr = std::shared_ptr<State>;

  enum class StartThreadReason {
    kInitialPool,
    kNoWaitersWhenScheduling,
    kNoWaitersWhenFinishedStarting,
  };

  static void ThreadFunc(StatePtr state);
  // Start a new thread; throttled indicates whether the State::starting_thread
  // variable is being used to throttle this threads creation against others or
  // not: at thread pool startup we start several threads concurrently, but
  // after that we only start one at a time.
  static void StartThread(StatePtr state, StartThreadReason reason);
  void Postfork();

  const unsigned reserve_threads_ =
      grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 32u);
  const StatePtr state_ = std::make_shared<State>(reserve_threads_);
  std::atomic<bool> quiesced_{false};
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
