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

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/thread_pool.h"

#include <atomic>
#include <memory>
#include <utility>

#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/support/log.h>

#include "src/core/lib/event_engine/work_queue.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

// TODO(hork): consider adding a lifeguard thread that starts worker threads if
// the backlog grows. This could be needed if Run is not called in a timely
// manner, which currently triggers the start of new threads.

namespace {
constexpr grpc_core::Duration kMaximumThreadStartFrequency =
    grpc_core::Duration::Milliseconds(1000);
constexpr grpc_core::Duration kBacklogWaitThreshold =
    grpc_core::Duration::Milliseconds(333);
// TODO(drfloob): Remove this, and replace it with the WorkQueue* for the
// current thread (with nullptr indicating not a threadpool thread).
thread_local bool g_threadpool_thread;
struct ThreadArg {
  bool initial;
  std::shared_ptr<ThreadPoolImpl> pool;
};
}  // namespace

class ThreadPoolImpl : public Forkable,
                       public Executor,
                       public std::enable_shared_from_this<ThreadPoolImpl> {
 public:
  ThreadPoolImpl();
  // Asserts Quiesce was called.
  ~ThreadPoolImpl() override;

  // two-stage initialization to support shared_from_this
  void Start();
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
  bool IsBacklogged();
  void SleepIfRunning();
  class ThreadCount {
   public:
    void Add();
    void Remove();
    void BlockUntilThreadCount(int threads, const char* why,
                               grpc_core::CondVar& waiting_cv);

   private:
    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_;
    int threads_ ABSL_GUARDED_BY(mu_) = 0;
  };

  enum class StartThreadReason {
    kInitialPool,
    kBacklogged,
  };

  static void WorkerThreadMain(std::shared_ptr<ThreadPoolImpl> pool);
  // Start a new thread.
  // if `initial=true`, this thread initializes the thread pool and is not
  // subject to rate limiting or backlog checking.
  void StartThread(bool initial);
  // Start a new thread while backlogged.
  // This is throttled to a maximum rate of thread creation, and only done if
  // the backlog necessitates it.
  void StartThreadIfBacklogged();
  void Postfork();

  WorkQueue global_queue_;
  ThreadCount thread_count_;
  // After pool creation we use this to rate limit creation of threads to one
  // at a time.
  std::atomic<bool> currently_starting_one_thread_;
  std::atomic<uint64_t> last_started_thread_;
  std::atomic<RunState> run_state_;

  const unsigned reserve_threads_;
  std::atomic<bool> quiesced_;
  std::atomic<uint32_t> threads_waiting_;
  grpc_core::Mutex wait_mu_;
  grpc_core::CondVar wait_cv_;
};

// ---- ThreadPool ----

ThreadPool::ThreadPool() : impl_(std::make_shared<ThreadPoolImpl>()) {
  impl_->Start();
}
void ThreadPool::Quiesce() { impl_->Quiesce(); }
void ThreadPool::Run(absl::AnyInvocable<void()> callback) {
  impl_->Run(std::move(callback));
};
void ThreadPool::Run(EventEngine::Closure* closure) { impl_->Run(closure); };
void ThreadPool::PrepareFork() { impl_->PrepareFork(); }
void ThreadPool::PostforkChild() { impl_->PostforkChild(); }
void ThreadPool::PostforkParent() { impl_->PostforkParent(); }

// ---- ThreadPoolImpl ----

ThreadPoolImpl::ThreadPoolImpl()
    : currently_starting_one_thread_(false),
      last_started_thread_(0),
      run_state_(RunState::kRunning),
      reserve_threads_(grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 32u)),
      quiesced_(false),
      threads_waiting_(0) {}

void ThreadPoolImpl::Start() {
  for (unsigned i = 0; i < reserve_threads_; i++) {
    StartThread(/*initial=*/true);
  }
}

void ThreadPoolImpl::StartThread(bool initial) {
  thread_count_.Add();
  last_started_thread_.store(
      grpc_core::Timestamp::Now().milliseconds_after_process_epoch(),
      std::memory_order_relaxed);
  // Initial pool creation can proceed immediately
  grpc_core::Thread(
      "event_engine_thread_pool",
      [](void* arg) {
        ThreadArg* thread_arg = static_cast<ThreadArg*>(arg);
        g_threadpool_thread = true;
        if (!thread_arg->initial) {
          thread_arg->pool->SleepIfRunning();
          GPR_ASSERT(thread_arg->pool->currently_starting_one_thread_.exchange(
              false, std::memory_order_relaxed));
          thread_arg->pool->StartThreadIfBacklogged();
        }
        WorkerThreadMain(std::move(thread_arg->pool));
        delete thread_arg;
      },
      new ThreadArg{initial, shared_from_this()}, nullptr,
      grpc_core::Thread::Options().set_tracked(false).set_joinable(false))
      .Start();
}

void ThreadPoolImpl::StartThreadIfBacklogged() {
  if (!IsBacklogged()) return;
  const auto now = grpc_core::Timestamp::Now();
  // Rate limit thread creation
  auto time_since_last_start =
      now - grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(
                last_started_thread_.load(std::memory_order_relaxed));
  if (time_since_last_start < kMaximumThreadStartFrequency) {
    return;
  }
  // Ensure only one thread is being created at a time
  if (currently_starting_one_thread_.exchange(true,
                                              std::memory_order_relaxed)) {
    return;
  }
  StartThread(/*initial=*/false);
}

void ThreadPoolImpl::WorkerThreadMain(std::shared_ptr<ThreadPoolImpl> pool) {
  while (pool->Step()) {
  }
  pool->thread_count_.Remove();
}

bool ThreadPoolImpl::Step() {
  // Wait until work is available or we are shutting down.
  while (run_state_ == RunState::kRunning && global_queue_.Empty()) {
    // If there are too many threads waiting, then quit this thread.
    if (threads_waiting_ >= reserve_threads_) {
      threads_waiting_++;
      grpc_core::MutexLock lock(&wait_mu_);
      bool timed_out = wait_cv_.WaitWithTimeout(&wait_mu_, absl::Seconds(30));
      threads_waiting_--;
      if (timed_out && threads_waiting_ >= reserve_threads_) {
        return false;
      }
    } else {
      threads_waiting_++;
      grpc_core::MutexLock lock(&wait_mu_);
      wait_cv_.Wait(&wait_mu_);
      threads_waiting_--;
    }
  }
  switch (run_state_) {
    case RunState::kRunning:
      break;
    case RunState::kShutdown:
    case RunState::kForking:
      if (!global_queue_.Empty()) break;
      return false;
  }
  auto callback = global_queue_.PopFront();
  if (callback != nullptr) {
    callback->Run();
  }
  return true;
}

void ThreadPoolImpl::Quiesce() {
  SetRunState(RunState::kShutdown);
  // Wait until all threads are exited.
  // Note that if this is a threadpool thread then we won't exit this thread
  // until the callstack unwinds a little, so we need to wait for just one
  // thread running instead of zero.
  thread_count_.BlockUntilThreadCount(g_threadpool_thread ? 1 : 0,
                                      "shutting down", wait_cv_);
  quiesced_.store(true, std::memory_order_relaxed);
}

ThreadPoolImpl::~ThreadPoolImpl() {
  GPR_ASSERT(quiesced_.load(std::memory_order_relaxed));
}

void ThreadPoolImpl::Run(absl::AnyInvocable<void()> callback) {
  GPR_DEBUG_ASSERT(quiesced_.load(std::memory_order_relaxed) == false);
  global_queue_.Add(std::move(callback));
  wait_cv_.Signal();
  if (run_state_ == RunState::kForking) return;
  StartThreadIfBacklogged();
}

void ThreadPoolImpl::Run(EventEngine::Closure* closure) {
  GPR_DEBUG_ASSERT(quiesced_.load(std::memory_order_relaxed) == false);
  global_queue_.Add(closure);
  wait_cv_.Signal();
  if (run_state_ == RunState::kForking) return;
  StartThreadIfBacklogged();
}

bool ThreadPoolImpl::IsBacklogged() {
  if (run_state_ == RunState::kForking) return false;
  auto oldest_ts = global_queue_.OldestEnqueuedTimestamp();
  // Has any callback been waiting too long?
  // TODO(hork): adjust this dynamically
  return (oldest_ts != grpc_core::Timestamp::InfPast()) &&
         (oldest_ts + kBacklogWaitThreshold) < grpc_core::Timestamp::Now();
}

void ThreadPoolImpl::SleepIfRunning() {
  auto end = grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(
                 last_started_thread_.load(std::memory_order_relaxed)) +
             kMaximumThreadStartFrequency;
  while (true) {
    if (run_state_ == RunState::kForking) return;
    grpc_core::Timestamp now = grpc_core::Timestamp::Now();
    if (now >= end) return;
    grpc_core::MutexLock lock(&wait_mu_);
    wait_cv_.WaitWithTimeout(&wait_mu_,
                             absl::Milliseconds((end - now).millis()));
  }
}

void ThreadPoolImpl::SetRunState(RunState state) {
  // TODO(hork): memory order?
  auto old_state = run_state_.exchange(state);
  if (state == RunState::kRunning) {
    GPR_ASSERT(old_state != RunState::kRunning);
  } else {
    GPR_ASSERT(old_state == RunState::kRunning);
  }
  wait_cv_.SignalAll();
}

void ThreadPoolImpl::ThreadCount::Add() {
  grpc_core::MutexLock lock(&mu_);
  ++threads_;
}

void ThreadPoolImpl::ThreadCount::Remove() {
  grpc_core::MutexLock lock(&mu_);
  --threads_;
  cv_.Signal();
}

void ThreadPoolImpl::ThreadCount::BlockUntilThreadCount(
    int threads, const char* why, grpc_core::CondVar& waiting_cv) {
  grpc_core::MutexLock lock(&mu_);
  auto last_log = absl::Now();
  while (threads_ > threads) {
    waiting_cv.SignalAll();
    // Wait for all threads to exit.
    // At least once every three seconds (but no faster than once per second
    // in the event of spurious wakeups) log a message indicating we're
    // waiting to fork.
    cv_.WaitWithTimeout(&mu_, absl::Seconds(3));
    if (threads_ > threads && absl::Now() - last_log > absl::Seconds(1)) {
      gpr_log(
          GPR_ERROR,
          "Waiting for thread pool to idle before %s. threads(%d) > goal(%d)",
          why, threads_, threads);
      last_log = absl::Now();
    }
  }
}

void ThreadPoolImpl::PrepareFork() {
  SetRunState(RunState::kForking);
  thread_count_.BlockUntilThreadCount(0, "forking", wait_cv_);
}
void ThreadPoolImpl::PostforkParent() { Postfork(); }
void ThreadPoolImpl::PostforkChild() { Postfork(); }
void ThreadPoolImpl::Postfork() {
  SetRunState(RunState::kRunning);
  for (unsigned i = 0; i < reserve_threads_; i++) {
    StartThread(/*initial=*/true);
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine
