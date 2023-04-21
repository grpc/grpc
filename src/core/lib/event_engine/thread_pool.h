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
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/cpu.h>

#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/event_engine/executor/executor.h"
#include "src/core/lib/event_engine/forkable.h"
#include "src/core/lib/event_engine/work_queue/basic_work_queue.h"
#include "src/core/lib/event_engine/work_queue/work_queue.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

class ThreadPool final : public Executor {
 public:
  ThreadPool();
  // Asserts Quiesce was called.
  ~ThreadPool() override;
  // Shut down the pool, and wait for all threads to exit.
  // This method is safe to call from within a ThreadPool thread.
  void Quiesce();
  // Run must not be called after Quiesce completes
  void Run(absl::AnyInvocable<void()> callback) override;
  void Run(EventEngine::Closure* closure) override;

  // This is test-only to support fork testing!
  void TestOnlyPrepareFork();
  void TestOnlyPostFork();

 private:
  // A basic communication mechanism to signal waiting threads that work is
  // available.
  class WorkSignal {
   public:
    void Signal();
    void SignalAll();
    // Returns whether a timeout occurred.
    bool WaitWithTimeout(grpc_core::Duration time);

   private:
    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_ ABSL_GUARDED_BY(mu_);
  };

  class ThreadCount {
   public:
    void Add();
    void Remove();
    void BlockUntilThreadCount(int desired_threads, const char* why,
                               WorkSignal* work_signal);
    int threads();

   private:
    std::atomic<int> threads_{0};
  };

  // Adds and removes thread counts on construction and destruction
  class AutoThreadCount {
   public:
    explicit AutoThreadCount(ThreadCount* counter) : counter_(counter) {
      counter_->Add();
    }
    ~AutoThreadCount() { counter_->Remove(); }

   private:
    ThreadCount* counter_;
  };

  // A pool of WorkQueues that participate in work stealing.
  //
  // Every worker thread registers and unregisters its thread-local thread pool
  // here, and steals closures from other threads when work is otherwise
  // unavailable.
  class TheftRegistry {
   public:
    // Allow any member of the registry to steal from the provided queue.
    void Enroll(WorkQueue* queue) ABSL_LOCKS_EXCLUDED(mu_);
    // Disallow work stealing from the provided queue.
    void Unenroll(WorkQueue* queue) ABSL_LOCKS_EXCLUDED(mu_);
    // Returns one closure from another thread, or nullptr if none are
    // available.
    EventEngine::Closure* StealOne() ABSL_LOCKS_EXCLUDED(mu_);

   private:
    grpc_core::Mutex mu_;
    absl::flat_hash_set<WorkQueue*> queues_ ABSL_GUARDED_BY(mu_);
  };

  enum class StartThreadReason {
    kInitialPool,
    kBackloggedWhenScheduling,
    kBackloggedWhenFinishedStarting,
  };

  class ThreadPoolImpl : public Forkable,
                         public std::enable_shared_from_this<ThreadPoolImpl> {
   public:
    const int reserve_threads_ = grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 8u);

    void Run(EventEngine::Closure* closure);
    // Returns true if a new thread should be created.
    bool IsBacklogged();
    // Start a new thread.
    // The reason argument determines whether thread creation is rate-limited;
    // threads created to populate the initial pool are not rate-limited, but
    // all others thread creation scenarios are rate-limited.
    void StartThread(StartThreadReason reason);
    // Sets a throttled state.
    // After the initial pool has been created, if the pool is backlogged when a
    // new thread has started, it is rate limited.
    // Returns the previous throttling state.
    bool SetThrottled(bool throttle);
    // Set the shutdown flag.
    void SetShutdown(bool is_shutdown);
    // Set the forking flag.
    void SetForking(bool is_forking);
    // Forkable
    // Ensures that the thread pool is empty before forking.
    // Postfork parent and child have the same behavior.
    void PrepareFork() override;
    void PostforkParent() override;
    void PostforkChild() override;
    void Postfork();
    // Accessor methods
    bool IsShutdown();
    bool IsForking();
    ThreadCount* thread_count() { return &thread_count_; }
    TheftRegistry* theft_registry() { return &theft_registry_; }
    WorkQueue* queue() { return &queue_; }
    WorkSignal* work_signal() { return &work_signal_; }

   private:
    ThreadCount thread_count_;
    TheftRegistry theft_registry_;
    BasicWorkQueue queue_;
    // Track shutdown and fork bits separately.
    // It's possible for a ThreadPool to initiate shut down while fork handlers
    // are running, and similarly possible for a fork event to occur during
    // shutdown.
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> forking_{false};
    std::atomic<uint64_t> last_started_thread_{0};
    // After pool creation we use this to rate limit creation of threads to one
    // at a time.
    std::atomic<bool> throttled_{false};
    WorkSignal work_signal_;
  };

  class ThreadState {
   public:
    ThreadState(std::shared_ptr<ThreadPoolImpl> pool,
                StartThreadReason start_reason);
    void ThreadBody();
    void SleepIfRunning();
    bool Step();

   private:
    // pool_ must be the first member so that it is alive when the thread count
    // is decremented at time of destruction. This is necessary when this thread
    // state holds the last shared_ptr keeping the pool alive.
    std::shared_ptr<ThreadPoolImpl> pool_;
    // auto_thread_count_ must be the second member declared, so that the thread
    // count is decremented after all other state is cleaned up (preventing
    // leaks).
    AutoThreadCount auto_thread_count_;
    StartThreadReason start_reason_;
    grpc_core::BackOff backoff_;
  };

  const std::shared_ptr<ThreadPoolImpl> pool_ =
      std::make_shared<ThreadPoolImpl>();
  std::atomic<bool> quiesced_{false};
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
