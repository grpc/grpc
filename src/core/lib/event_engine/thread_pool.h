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
#include "src/core/lib/gprpp/thd.h"

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

  // A pool of WorkQueues that participate in work stealing.
  //
  // Every worker thread registers and unregisters its thread-local thread pool
  // here, and steals closures when necessary.
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
    BackloggedWhenScheduling,
    BackloggedWhenFinishedStarting,
  };

  class ThreadPoolImpl {
   public:
    const unsigned reserve_threads_ =
        grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 8u);

    void Run(EventEngine::Closure* closure);
    // Returns true if a new thread should be created.
    bool IsBacklogged();
    // Start a new thread.
    // The reason parameter determines whether thread creation is rate-limited;
    // threads created to populate the initial pool are not rate-limited, but
    // all others scenarios are.
    void StartThread(ThreadPoolImpl* pool, StartThreadReason reason);
    // Sets a throttled state.
    // After the initial pool has been created, if the pool is backlogged when a
    // new thread has started, it is rate limited.
    // Returns whether the pool was already throttled.
    bool SetThrottled(bool throttle);
    void SetShutdown(bool is_shutdown);
    void SetForking(bool is_forking);
    // accessors
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
    ThreadState(ThreadPoolImpl* pool, StartThreadReason start_reason);
    void ThreadBody();
    void SleepIfRunning();
    bool Step();

   private:
    ThreadPoolImpl* pool_;
    StartThreadReason start_reason_;
    grpc_core::BackOff backoff_;
  };

  void Postfork();

  // Returns true if the current thread is a thread pool thread.
  static bool IsThreadPoolThread();

  const std::shared_ptr<ThreadPoolImpl> state_ =
      std::make_shared<ThreadPoolImpl>();
  std::atomic<bool> quiesced_{false};
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
