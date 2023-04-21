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

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/thread_pool.h"

#include <atomic>
#include <memory>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/support/log.h>

#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/thread_local.h"
#include "src/core/lib/event_engine/work_queue/basic_work_queue.h"
#include "src/core/lib/event_engine/work_queue/work_queue.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
constexpr grpc_core::Duration kIdleThreadLimit =
    grpc_core::Duration::Seconds(20);
constexpr grpc_core::Duration kThrottledThreadStartRate =
    grpc_core::Duration::Seconds(1);
}  // namespace

thread_local WorkQueue* g_local_queue = nullptr;

// -------- ThreadPool --------

ThreadPool::ThreadPool() {
  for (unsigned i = 0; i < pool_->reserve_threads_; i++) {
    pool_->StartThread(StartThreadReason::kInitialPool);
  }
}

void ThreadPool::Quiesce() {
  pool_->SetShutdown(true);
  // Wait until all threads have exited.
  // Note that if this is a threadpool thread then we won't exit this thread
  // until all other threads have exited, so we need to wait for just one thread
  // running instead of zero.
  bool is_threadpool_thread = g_local_queue != nullptr;
  pool_->thread_count()->BlockUntilThreadCount(
      is_threadpool_thread ? 1 : 0, "shutting down", pool_->work_signal());
  GPR_ASSERT(pool_->queue()->Empty());
  quiesced_.store(true, std::memory_order_relaxed);
}

ThreadPool::~ThreadPool() {
  GPR_ASSERT(quiesced_.load(std::memory_order_relaxed));
}

void ThreadPool::Run(absl::AnyInvocable<void()> callback) {
  Run(SelfDeletingClosure::Create(std::move(callback)));
}

void ThreadPool::Run(EventEngine::Closure* closure) {
  GPR_DEBUG_ASSERT(quiesced_.load(std::memory_order_relaxed) == false);
  pool_->Run(closure);
}

// -------- ThreadPool::TheftRegistry --------

void ThreadPool::TheftRegistry::Enroll(WorkQueue* queue) {
  grpc_core::MutexLock lock(&mu_);
  queues_.emplace(queue);
}

void ThreadPool::TheftRegistry::Unenroll(WorkQueue* queue) {
  grpc_core::MutexLock lock(&mu_);
  queues_.erase(queue);
}

EventEngine::Closure* ThreadPool::TheftRegistry::StealOne() {
  grpc_core::MutexLock lock(&mu_);
  EventEngine::Closure* closure;
  for (auto* queue : queues_) {
    closure = queue->PopMostRecent();
    if (closure != nullptr) return closure;
  }
  return nullptr;
}

void ThreadPool::TestOnlyPrepareFork() { pool_->PrepareFork(); }

void ThreadPool::TestOnlyPostFork() { pool_->Postfork(); }

// -------- ThreadPool::ThreadPoolImpl --------

void ThreadPool::ThreadPoolImpl::Run(EventEngine::Closure* closure) {
  // TODO(hork): move the backlog check elsewhere so the local run path can
  // remain a tight loop. It's now only checked when an external closure is
  // added.
  if (g_local_queue != nullptr) {
    g_local_queue->Add(closure);
    return;
  }
  queue_.Add(closure);
  work_signal_.Signal();
  if (IsBacklogged()) {
    StartThread(StartThreadReason::kBackloggedWhenScheduling);
  }
}

void ThreadPool::ThreadPoolImpl::StartThread(StartThreadReason reason) {
  thread_count_.Add();
  const auto now = grpc_core::Timestamp::Now();
  switch (reason) {
    case StartThreadReason::kBackloggedWhenScheduling: {
      auto time_since_last_start =
          now - grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(
                    last_started_thread_.load(std::memory_order_relaxed));
      if (time_since_last_start < kThrottledThreadStartRate) {
        thread_count_.Remove();
        return;
      }
    }
      ABSL_FALLTHROUGH_INTENDED;
    case StartThreadReason::kBackloggedWhenFinishedStarting:
      if (SetThrottled(true)) {
        thread_count_.Remove();
        return;
      }
      last_started_thread_.store(now.milliseconds_after_process_epoch(),
                                 std::memory_order_relaxed);
      break;
    case StartThreadReason::kInitialPool:
      break;
  }
  grpc_core::Thread(
      "event_engine",
      [](void* arg) {
        std::unique_ptr<ThreadState> worker(static_cast<ThreadState*>(arg));
        worker->ThreadBody();
      },
      new ThreadState(shared_from_this(), reason), nullptr,
      grpc_core::Thread::Options().set_tracked(false).set_joinable(false))
      .Start();
}

bool ThreadPool::ThreadPoolImpl::SetThrottled(bool throttled) {
  return throttled_.exchange(throttled, std::memory_order_relaxed);
}

void ThreadPool::ThreadPoolImpl::SetShutdown(bool is_shutdown) {
  auto was_shutdown = shutdown_.exchange(is_shutdown);
  GPR_ASSERT(is_shutdown != was_shutdown);
  work_signal_.SignalAll();
}

void ThreadPool::ThreadPoolImpl::SetForking(bool is_forking) {
  auto was_forking = forking_.exchange(is_forking);
  GPR_ASSERT(is_forking != was_forking);
}

bool ThreadPool::ThreadPoolImpl::IsBacklogged() {
  if (forking_.load()) return false;
  // DO NOT SUBMIT(hork): better heuristic
  return queue()->Size() > 10;
}

bool ThreadPool::ThreadPoolImpl::IsForking() {
  return forking_.load(std::memory_order_relaxed);
}

bool ThreadPool::ThreadPoolImpl::IsShutdown() {
  return shutdown_.load(std::memory_order_relaxed);
}

void ThreadPool::ThreadPoolImpl::PrepareFork() {
  SetForking(true);
  thread_count()->BlockUntilThreadCount(0, "forking", &work_signal_);
}

void ThreadPool::ThreadPoolImpl::PostforkParent() { Postfork(); }

void ThreadPool::ThreadPoolImpl::PostforkChild() { Postfork(); }

void ThreadPool::ThreadPoolImpl::Postfork() {
  SetForking(false);
  for (unsigned i = 0; i < reserve_threads_; i++) {
    StartThread(StartThreadReason::kInitialPool);
  }
}

// -------- ThreadPool::ThreadState --------

ThreadPool::ThreadState::ThreadState(std::shared_ptr<ThreadPoolImpl> pool,
                                     StartThreadReason start_reason)
    : pool_(std::move(pool)),
      start_reason_(start_reason),
      backoff_(grpc_core::BackOff::Options()
                   .set_initial_backoff(grpc_core::Duration::Milliseconds(33))
                   .set_max_backoff(grpc_core::Duration::Seconds(3))
                   .set_multiplier(1.3)) {}

void ThreadPool::ThreadState::ThreadBody() {
  g_local_queue = new BasicWorkQueue();
  pool_->theft_registry()->Enroll(g_local_queue);
  ThreadLocal::SetIsEventEngineThread(true);
  switch (start_reason_) {
    case StartThreadReason::kInitialPool:
      break;
    case StartThreadReason::kBackloggedWhenFinishedStarting:
      SleepIfRunning();
      ABSL_FALLTHROUGH_INTENDED;
    case StartThreadReason::kBackloggedWhenScheduling:
      GPR_ASSERT(pool_->SetThrottled(false));
      if (pool_->IsBacklogged()) {
        pool_->StartThread(StartThreadReason::kBackloggedWhenFinishedStarting);
      }
      break;
  }
  while (Step()) {
    // loop until the thread should no longer run
  }
  // cleanup
  if (pool_->IsForking()) {
    // TODO(hork): consider WorkQueue::AddAll(WorkQueue*)
    EventEngine::Closure* closure;
    while (!g_local_queue->Empty()) {
      closure = g_local_queue->PopMostRecent();
      if (closure != nullptr) pool_->queue()->Add(closure);
    }
  }
  GPR_ASSERT(g_local_queue->Empty());
  pool_->theft_registry()->Unenroll(g_local_queue);
  delete g_local_queue;
  pool_->thread_count()->Remove();
}

void ThreadPool::ThreadState::SleepIfRunning() {
  if (pool_->IsForking()) return;
  absl::SleepFor(absl::Milliseconds(kThrottledThreadStartRate.millis()));
}

bool ThreadPool::ThreadState::Step() {
  if (pool_->IsForking()) return false;
  auto* closure = g_local_queue->PopMostRecent();
  // If local work is available, run it.
  if (closure != nullptr) {
    closure->Run();
    return true;
  }
  // Thread shutdown exit condition (ignoring fork). All must be true:
  // * shutdown was called
  // * the local queue is empty
  // * the global queue is empty
  // * the steal pool returns nullptr
  bool should_run_again = false;
  grpc_core::Timestamp start_time{grpc_core::Timestamp::Now()};
  // Wait until work is available or until shut down.
  while (!pool_->IsForking()) {
    // Pull from the global queue next
    if (!pool_->queue()->Empty()) {
      should_run_again = true;
      closure = pool_->queue()->PopMostRecent();
      break;
    };
    // Try stealing if the queue is empty
    closure = pool_->theft_registry()->StealOne();
    if (closure != nullptr) {
      should_run_again = true;
      break;
    }
    // No closures were retrieved from anywhere.
    // Quit the thread if the pool has been shut down.
    if (pool_->IsShutdown()) break;
    bool timed_out = pool_->work_signal()->WaitWithTimeout(
        backoff_.NextAttemptTime() - grpc_core::Timestamp::Now());
    // Quit a thread if the pool has more than it requires, and this thread has
    // been idle long enough.
    if (timed_out &&
        pool_->thread_count()->threads() > pool_->reserve_threads_ &&
        grpc_core::Timestamp::Now() - start_time > kIdleThreadLimit) {
      return false;
    }
  }
  if (pool_->IsForking()) {
    // save the closure since we aren't going to execute it.
    if (closure != nullptr) g_local_queue->Add(closure);
    return false;
  }
  if (closure != nullptr) closure->Run();
  backoff_.Reset();
  return should_run_again;
}

// -------- ThreadPool::ThreadCount --------

void ThreadPool::ThreadCount::Add() {
  threads_.fetch_add(1, std::memory_order_relaxed);
}

void ThreadPool::ThreadCount::Remove() {
  threads_.fetch_sub(1, std::memory_order_relaxed);
}

void ThreadPool::ThreadCount::BlockUntilThreadCount(int desired_threads,
                                                    const char* why,
                                                    WorkSignal* work_signal) {
  int curr_threads = threads_.load(std::memory_order_relaxed);
  // Wait for all threads to exit.
  auto last_log_time = grpc_core::Timestamp::Now();
  while (curr_threads > desired_threads) {
    absl::SleepFor(absl::Milliseconds(100));
    work_signal->SignalAll();
    if (grpc_core::Timestamp::Now() - last_log_time >
        grpc_core::Duration::Seconds(3)) {
      gpr_log(GPR_DEBUG,
              "Waiting for thread pool to idle before %s. (%d to %d)", why,
              curr_threads, desired_threads);
      last_log_time = grpc_core::Timestamp::Now();
    }
    curr_threads = threads_.load(std::memory_order_relaxed);
  }
}

int ThreadPool::ThreadCount::threads() {
  return threads_.load(std::memory_order_relaxed);
}

// -------- ThreadPool::WorkSignal --------

void ThreadPool::WorkSignal::Signal() {
  grpc_core::MutexLock lock(&mu_);
  cv_.Signal();
}

void ThreadPool::WorkSignal::SignalAll() {
  grpc_core::MutexLock lock(&mu_);
  cv_.SignalAll();
}

bool ThreadPool::WorkSignal::WaitWithTimeout(grpc_core::Duration time) {
  grpc_core::MutexLock lock(&mu_);
  return cv_.WaitWithTimeout(&mu_, absl::Milliseconds(time.millis()));
}

}  // namespace experimental
}  // namespace grpc_event_engine
