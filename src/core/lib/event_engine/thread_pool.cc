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

#include "src/core/lib/event_engine/thread_local.h"
#include "src/core/lib/event_engine/work_queue/basic_work_queue.h"
#include "src/core/lib/event_engine/work_queue/work_queue.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

thread_local WorkQueue* g_local_queue = nullptr;

// -------- ThreadPool --------

void ThreadPool::StartThread(ThreadPoolImplPtr state,
                             StartThreadReason reason) {
  state->thread_count.Add();
  const auto now = grpc_core::Timestamp::Now();
  switch (reason) {
    case StartThreadReason::kNoWaitersWhenScheduling: {
      auto time_since_last_start =
          now - grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(
                    state->last_started_thread.load(std::memory_order_relaxed));
      if (time_since_last_start < grpc_core::Duration::Seconds(1)) {
        state->thread_count.Remove();
        return;
      }
    }
      ABSL_FALLTHROUGH_INTENDED;
    case StartThreadReason::kNoWaitersWhenFinishedStarting:
      if (state->currently_starting_one_thread.exchange(
              true, std::memory_order_relaxed)) {
        state->thread_count.Remove();
        return;
      }
      state->last_started_thread.store(now.milliseconds_after_process_epoch(),
                                       std::memory_order_relaxed);
      break;
    case StartThreadReason::kInitialPool:
      break;
  }
  grpc_core::Thread(
      "event_engine",
      [](void* arg) {
        std::unique_ptr<ThreadState> worker(static_cast<ThreadState*>(arg));
        g_local_queue = new BasicWorkQueue();
        worker->pool->theft_registry.Enroll(g_local_queue);
        ThreadLocal::SetIsEventEngineThread(true);
        switch (worker->start_reason) {
          case StartThreadReason::kInitialPool:
            break;
          case StartThreadReason::kNoWaitersWhenFinishedStarting:
            worker->SleepIfRunning();
            ABSL_FALLTHROUGH_INTENDED;
          case StartThreadReason::kNoWaitersWhenScheduling:
            // Release throttling variable
            GPR_ASSERT(worker->pool->currently_starting_one_thread.exchange(
                false, std::memory_order_relaxed));
            if (worker->pool->IsBacklogged()) {
              StartThread(worker->pool,
                          StartThreadReason::kNoWaitersWhenFinishedStarting);
            }
            break;
        }
        worker->ThreadBody();
        // cleanup
        if (worker->pool->forking.load()) {
          // TODO(hork): consider WorkQueue::AddAll(WorkQueue*)
          EventEngine::Closure* closure;
          while (!g_local_queue->Empty()) {
            closure = g_local_queue->PopMostRecent();
            if (closure != nullptr) worker->pool->queue.Add(closure);
          }
        }
        GPR_ASSERT(g_local_queue->Empty());
        worker->pool->theft_registry.Unenroll(g_local_queue);
        delete g_local_queue;
      },
      new ThreadState{state, reason}, nullptr,
      grpc_core::Thread::Options().set_tracked(false).set_joinable(false))
      .Start();
}

ThreadPool::ThreadPool() {
  for (unsigned i = 0; i < state_->reserve_threads_; i++) {
    StartThread(state_, StartThreadReason::kInitialPool);
  }
}

bool ThreadPool::IsThreadPoolThread() { return g_local_queue != nullptr; }

void ThreadPool::Quiesce() {
  SetShutdown(true);
  // Wait until all threads are exited.
  // Note that if this is a threadpool thread then we won't exit this thread
  // until the callstack unwinds a little, so we need to wait for just one
  // thread running instead of zero.
  state_->thread_count.BlockUntilThreadCount(IsThreadPoolThread() ? 1 : 0,
                                             "shutting down");
  quiesced_.store(true, std::memory_order_relaxed);
}

ThreadPool::~ThreadPool() {
  GPR_ASSERT(quiesced_.load(std::memory_order_relaxed));
}

void ThreadPool::Run(absl::AnyInvocable<void()> callback) {
  GPR_DEBUG_ASSERT(quiesced_.load(std::memory_order_relaxed) == false);
  // TODO(hork): move the backlog check elsewhere so the local run path can
  // remain a tight loop. It's now only checked when an external closure is
  // added.
  if (g_local_queue != nullptr) {
    g_local_queue->Add(std::move(callback));
    return;
  }
  state_->queue.Add(std::move(callback));
  bool should_start_new_thread = false;
  {
    grpc_core::MutexLock lock(&state_->mu);
    state_->broadcast.Signal();
    should_start_new_thread = state_->IsBackloggedLocked();
  }
  if (should_start_new_thread) {
    StartThread(state_, StartThreadReason::kNoWaitersWhenScheduling);
  }
}

void ThreadPool::Run(EventEngine::Closure* closure) {
  Run([closure]() { closure->Run(); });
}

void ThreadPool::SetShutdown(bool is_shutdown) {
  grpc_core::MutexLock lock(&state_->mu);
  auto was_shutdown = std::exchange(state_->shutdown, is_shutdown);
  GPR_ASSERT(is_shutdown != was_shutdown);
  state_->broadcast.SignalAll();
}

void ThreadPool::SetForking(bool is_forking) {
  auto was_forking = state_->forking.exchange(is_forking);
  GPR_ASSERT(is_forking != was_forking);
  grpc_core::MutexLock lock(&state_->mu);
  state_->broadcast.SignalAll();
}

void ThreadPool::PrepareFork() {
  SetForking(true);
  state_->thread_count.BlockUntilThreadCount(0, "forking");
}

void ThreadPool::PostforkParent() { Postfork(); }

void ThreadPool::PostforkChild() { Postfork(); }

void ThreadPool::Postfork() {
  SetForking(false);
  for (unsigned i = 0; i < state_->reserve_threads_; i++) {
    StartThread(state_, StartThreadReason::kInitialPool);
  }
}

// -------- ThreadPool::TheifRegistry --------

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
  for (auto& queue : queues_) {
    closure = queue->PopMostRecent();
    if (closure != nullptr) {
      return closure;
    }
  }
  return nullptr;
}

// -------- ThreadPool::ThreadPoolState --------

bool ThreadPool::ThreadPoolImpl::IsBacklogged() {
  grpc_core::MutexLock lock(&mu);
  return IsBackloggedLocked();
}

bool ThreadPool::ThreadPoolImpl::IsBackloggedLocked() {
  if (forking.load()) return false;
  // DO NOT SUBMIT(hork): better heuristic
  return queue.Size() > 10;
}

// -------- ThreadPool::ThreadState --------

void ThreadPool::ThreadState::ThreadBody() {
  while (Step()) {
  }
  pool->thread_count.Remove();
}

void ThreadPool::ThreadState::SleepIfRunning() {
  auto end = grpc_core::Duration::Seconds(1) + grpc_core::Timestamp::Now();
  grpc_core::MutexLock lock(&pool->mu);
  while (true) {
    grpc_core::Timestamp now = grpc_core::Timestamp::Now();
    if (now >= end || pool->forking.load()) return;
    pool->broadcast.WaitWithTimeout(&pool->mu,
                                    absl::Milliseconds((end - now).millis()));
  }
}

bool ThreadPool::ThreadState::Step() {
  if (pool->forking.load()) return false;
  auto* closure = g_local_queue->PopMostRecent();
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
  {
    grpc_core::MutexLock lock(&pool->mu);
    // Wait until work is available or until shut down.
    while (!pool->forking.load()) {
      // Pull from the global queue first
      if (!pool->queue.Empty()) {
        should_run_again = true;
        closure = pool->queue.PopMostRecent();
        break;
      };
      // Try stealing if the queue is empty
      closure = pool->theft_registry.StealOne();
      if (closure != nullptr) {
        should_run_again = true;
        break;
      }
      // No closures were retrieved from anywhere. Shut down the thread if the
      // pool has been shut down.
      if (pool->shutdown) break;
      // Otherwise, hang out a while, waiting for work to arrive.
      // TODO(hork): consider using a timeout backoff.
      bool timed_out =
          WaitForWorkLocked(pool->threads_waiting_ >= pool->reserve_threads_
                                ? absl::Seconds(30)
                                : absl::InfiniteDuration());
      if (timed_out) {
        return false;
      }
    }
  }
  if (pool->forking.load()) {
    // save the closure since we aren't going to execute it.
    if (closure != nullptr) g_local_queue->Add(closure);
    return false;
  }
  if (closure != nullptr) closure->Run();
  return should_run_again;
}

bool ThreadPool::ThreadState::WaitForWorkLocked(absl::Duration wait) {
  pool->threads_waiting_++;
  bool timed_out = false;
  timed_out = pool->broadcast.WaitWithTimeout(&pool->mu, wait);
  pool->threads_waiting_--;
  return timed_out;
}

// -------- ThreadPool::ThreadCount --------

void ThreadPool::ThreadCount::Add() {
  grpc_core::MutexLock lock(&thread_count_mu_);
  ++threads_;
}

void ThreadPool::ThreadCount::Remove() {
  grpc_core::MutexLock lock(&thread_count_mu_);
  --threads_;
  cv_.Signal();
}

void ThreadPool::ThreadCount::BlockUntilThreadCount(int threads,
                                                    const char* why) {
  grpc_core::MutexLock lock(&thread_count_mu_);
  auto last_log = absl::Now();
  while (threads_ > threads) {
    // Wait for all threads to exit.
    // At least once every three seconds (but no faster than once per second
    // in the event of spurious wakeups) log a message indicating we're
    // waiting to fork.
    cv_.WaitWithTimeout(&thread_count_mu_, absl::Seconds(3));
    if (threads_ > threads && absl::Now() - last_log > absl::Seconds(1)) {
      gpr_log(GPR_ERROR,
              "Waiting for thread pool to idle before %s. (%d to %d)", why,
              threads_, threads);
      last_log = absl::Now();
    }
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine
