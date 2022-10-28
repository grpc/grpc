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
#include "src/core/lib/event_engine/work_queue.h"
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
  enum class RunState { kRunning, kShutdown, kForking };
  bool Step();
  void SetRunState(RunState state);
  // DO NOT SUBMIT(hork): implement a backlog check (currently: queue > 1)
  bool IsBacklogged();
  // DO NOT SUBMIT(hork): implement SleepIfRunning (trickled thread creation on
  // startup)
  void SleepIfRunning();
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

  enum class StartThreadReason {
    kInitialPool,
    kBacklogged,
  };

  static void ThreadFunc(ThreadPool* pool);
  // Start a new thread that initializes the thread pool
  // This is not subject to rate limiting or backlog checking.
  void StartInitialThread();
  // Start a new thread while backlogged.
  // This is throttled to a maximum rate of thread creation, and only done if
  // the backlog necessitates it.
  void StartThreadIfBacklogged() ABSL_LOCKS_EXCLUDED(run_state_mu_);
  void Postfork();

  WorkQueue global_queue_;
  ThreadCount thread_count_;
  // After pool creation we use this to rate limit creation of threads to one
  // at a time.
  std::atomic<bool> currently_starting_one_thread_;
  std::atomic<uint64_t> last_started_thread_;
  RunState run_state_ ABSL_GUARDED_BY(run_state_mu_);

  const unsigned reserve_threads_;
  std::atomic<bool> quiesced_;
  grpc_core::Mutex run_state_mu_;
  grpc_core::CondVar run_state_cv_;
  unsigned threads_waiting_ ABSL_GUARDED_BY(run_state_mu_);
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
