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

#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

constexpr grpc_core::Duration kMaximumThreadStartFrequency =
    grpc_core::Duration::Milliseconds(1000);
constexpr grpc_core::Duration kBacklogWaitThreshold =
    grpc_core::Duration::Milliseconds(333);

// TODO(hork): consider adding a lifeguard thread that starts worker threads if
// the backlog grows. This could be needed if Run is not called in a timely
// manner, which currently triggers the start of new threads.

namespace {
// TODO(drfloob): Remove this, and replace it with the WorkQueue* for the
// current thread (with nullptr indicating not a threadpool thread).
thread_local bool g_threadpool_thread;
}  // namespace

ThreadPool::ThreadPool()
    : currently_starting_one_thread_(false),
      last_started_thread_(0),
      run_state_(RunState::kRunning),
      reserve_threads_(grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 32u)),
      quiesced_(false),
      threads_waiting_(0) {
  for (unsigned i = 0; i < reserve_threads_; i++) {
    StartInitialThread();
  }
}

void ThreadPool::StartInitialThread() {
  thread_count_.Add();
  last_started_thread_.store(
      grpc_core::Timestamp::Now().milliseconds_after_process_epoch(),
      std::memory_order_relaxed);
  // Initial pool creation can proceed immediately
  grpc_core::Thread(
      "event_engine_thread_pool",
      [](void* arg) {
        ThreadPool* pool = static_cast<ThreadPool*>(arg);
        g_threadpool_thread = true;
        ThreadFunc(pool);
      },
      this, nullptr,
      grpc_core::Thread::Options().set_tracked(false).set_joinable(false))
      .Start();
}

void ThreadPool::StartThreadIfBacklogged() {
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
  thread_count_.Add();
  last_started_thread_.store(now.milliseconds_after_process_epoch(),
                             std::memory_order_relaxed);
  grpc_core::Thread(
      "event_engine_thread_pool",
      [](void* arg) {
        ThreadPool* pool = static_cast<ThreadPool*>(arg);
        g_threadpool_thread = true;
        pool->SleepIfRunning();
        GPR_ASSERT(pool->currently_starting_one_thread_.exchange(
            false, std::memory_order_relaxed));
        pool->StartThreadIfBacklogged();
        ThreadFunc(pool);
      },
      this, nullptr,
      grpc_core::Thread::Options().set_tracked(false).set_joinable(false))
      .Start();
}

void ThreadPool::ThreadFunc(ThreadPool* pool) {
  while (pool->Step()) {
  }
  pool->thread_count_.Remove();
}

bool ThreadPool::Step() {
  grpc_core::ReleasableMutexLock lock(&run_state_mu_);
  // Wait until work is available or we are shutting down.
  while (run_state_ == RunState::kRunning && global_queue_.Empty()) {
    // If there are too many threads waiting, then quit this thread.
    if (threads_waiting_ >= reserve_threads_) {
      threads_waiting_++;
      bool timeout =
          run_state_cv_.WaitWithTimeout(&run_state_mu_, absl::Seconds(30));
      threads_waiting_--;
      if (timeout && threads_waiting_ >= reserve_threads_) {
        return false;
      }
    } else {
      threads_waiting_++;
      run_state_cv_.Wait(&run_state_mu_);
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
  GPR_ASSERT(!global_queue_.Empty());
  auto callback = global_queue_.PopFront();
  lock.Release();
  if (callback != nullptr) {
    callback->Run();
  }
  return true;
}

void ThreadPool::Quiesce() {
  SetRunState(RunState::kShutdown);
  // Wait until all threads are exited.
  // Note that if this is a threadpool thread then we won't exit this thread
  // until the callstack unwinds a little, so we need to wait for just one
  // thread running instead of zero.
  thread_count_.BlockUntilThreadCount(g_threadpool_thread ? 1 : 0,
                                      "shutting down");
  quiesced_.store(true, std::memory_order_relaxed);
}

ThreadPool::~ThreadPool() {
  GPR_ASSERT(quiesced_.load(std::memory_order_relaxed));
}

void ThreadPool::Run(absl::AnyInvocable<void()> callback) {
  GPR_DEBUG_ASSERT(quiesced_.load(std::memory_order_relaxed) == false);
  global_queue_.Add(std::move(callback));
  run_state_cv_.Signal();
  {
    grpc_core::MutexLock lock(&run_state_mu_);
    if (run_state_ == RunState::kForking) return;
  }
  StartThreadIfBacklogged();
}

void ThreadPool::Run(EventEngine::Closure* closure) {
  GPR_DEBUG_ASSERT(quiesced_.load(std::memory_order_relaxed) == false);
  global_queue_.Add(closure);
  run_state_cv_.Signal();
  {
    grpc_core::MutexLock lock(&run_state_mu_);
    if (run_state_ == RunState::kForking) return;
  }
  StartThreadIfBacklogged();
}

bool ThreadPool::IsBacklogged() {
  grpc_core::MutexLock lock(&run_state_mu_);
  if (run_state_ == RunState::kForking) return false;
  auto oldest_ts = global_queue_.OldestEnqueuedTimestamp();
  // Has any callback been waiting too long?
  // TODO(hork): adjust this dynamically
  return (oldest_ts != grpc_core::Timestamp::InfPast()) &&
         (oldest_ts + kBacklogWaitThreshold) < grpc_core::Timestamp::Now();
}

void ThreadPool::SleepIfRunning() {
  grpc_core::MutexLock lock(&run_state_mu_);
  auto end = grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(
                 last_started_thread_.load(std::memory_order_relaxed)) +
             kMaximumThreadStartFrequency;
  while (true) {
    if (run_state_ == RunState::kForking) return;
    grpc_core::Timestamp now = grpc_core::Timestamp::Now();
    if (now >= end) return;
    run_state_cv_.WaitWithTimeout(&run_state_mu_,
                                  absl::Milliseconds((end - now).millis()));
  }
}

void ThreadPool::SetRunState(RunState state) {
  grpc_core::MutexLock lock(&run_state_mu_);
  if (state == RunState::kRunning) {
    GPR_ASSERT(run_state_ != RunState::kRunning);
  } else {
    GPR_ASSERT(run_state_ == RunState::kRunning);
  }
  run_state_ = state;
  run_state_cv_.SignalAll();
}

void ThreadPool::ThreadCount::Add() {
  grpc_core::MutexLock lock(&mu_);
  ++threads_;
  gpr_log(GPR_DEBUG, "DO NOT SUBMIT: ThreadCount::Add threads=%d", threads_);
}

void ThreadPool::ThreadCount::Remove() {
  grpc_core::MutexLock lock(&mu_);
  --threads_;
  cv_.Signal();
  gpr_log(GPR_DEBUG, "DO NOT SUBMIT: ThreadCount::Remove threads=%d", threads_);
}

void ThreadPool::ThreadCount::BlockUntilThreadCount(int threads,
                                                    const char* why) {
  grpc_core::MutexLock lock(&mu_);
  auto last_log = absl::Now();
  while (threads_ > threads) {
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

void ThreadPool::PrepareFork() {
  SetRunState(RunState::kForking);
  thread_count_.BlockUntilThreadCount(0, "forking");
}
void ThreadPool::PostforkParent() { Postfork(); }
void ThreadPool::PostforkChild() { Postfork(); }
void ThreadPool::Postfork() {
  SetRunState(RunState::kRunning);
  for (unsigned i = 0; i < reserve_threads_; i++) {
    StartInitialThread();
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine
