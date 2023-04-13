//
//
// Copyright 2015 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <atomic>
#include <memory>

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

  // Returns true if the current thread is a thread pool thread.
  static bool IsThreadPoolThread();

 private:
  class ThreadCount {
   public:
    void Add();
    void Remove();
    void BlockUntilThreadCount(int threads, const char* why);

   private:
    grpc_core::Mutex thread_count_mu_;
    grpc_core::CondVar cv_;
    int threads_ ABSL_GUARDED_BY(thread_count_mu_) = 0;
  };

  struct ThreadPoolState {
    // Returns true if a new thread should be created.
    bool IsBacklogged() ABSL_LOCKS_EXCLUDED(mu);
    bool IsBackloggedLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu);
    bool Step();

    const unsigned reserve_threads_ =
        grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 32u);
    WorkQueue queue;
    ThreadCount thread_count;
    // After pool creation we use this to rate limit creation of threads to one
    // at a time.
    std::atomic<bool> currently_starting_one_thread{false};
    std::atomic<uint64_t> last_started_thread{0};
    // Track shutdown and fork bits separately.
    // It's possible for a ThreadPool to initiate shut down while fork handlers
    // are running, and similarly possible for a fork event to occur during
    // shutdown.
    grpc_core::Mutex mu;
    unsigned threads_waiting_ ABSL_GUARDED_BY(mu) = 0;
    grpc_core::CondVar broadcast ABSL_GUARDED_BY(mu);
    bool shutdown ABSL_GUARDED_BY(mu) = false;
    bool forking ABSL_GUARDED_BY(mu) = false;
  };

  using ThreadPoolStatePtr = std::shared_ptr<ThreadPoolState>;

  enum class StartThreadReason {
    kInitialPool,
    kNoWaitersWhenScheduling,
    kNoWaitersWhenFinishedStarting,
  };

  void SetShutdown(bool is_shutdown);
  void SetForking(bool is_forking);

  static void ThreadFunc(ThreadPoolStatePtr state);
  // Start a new thread; throttled indicates whether the State::starting_thread
  // variable is being used to throttle this threads creation against others or
  // not: at thread pool startup we start several threads concurrently, but
  // after that we only start one at a time.
  static void StartThread(ThreadPoolStatePtr state, StartThreadReason reason);
  void Postfork();

  const ThreadPoolStatePtr state_ = std::make_shared<ThreadPoolState>();
  std::atomic<bool> quiesced_{false};
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
