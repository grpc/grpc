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
#include "work_queue/basic_work_queue.h"

#include <grpc/support/log.h>

#include "src/core/lib/event_engine/thread_local.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

thread_local BasicWorkQueue* g_local_queue = nullptr;

void ThreadPool::StartThread(ThreadPoolStatePtr state,
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
  struct ThreadState {
    ThreadPoolStatePtr state;
    StartThreadReason reason;
    void SleepIfRunning() {
      auto end = grpc_core::Duration::Seconds(1) + grpc_core::Timestamp::Now();
      grpc_core::MutexLock lock(&state->mu);
      while (true) {
        grpc_core::Timestamp now = grpc_core::Timestamp::Now();
        if (now >= end || state->forking) return;
        state->broadcast.WaitWithTimeout(
            &state->mu, absl::Milliseconds((end - now).millis()));
      }
    }
  };
  grpc_core::Thread(
      "event_engine",
      [](void* arg) {
        g_local_queue = new BasicWorkQueue();
        std::unique_ptr<ThreadState> a(static_cast<ThreadState*>(arg));
        ThreadLocal::SetIsEventEngineThread(true);
        switch (a->reason) {
          case StartThreadReason::kInitialPool:
            break;
          case StartThreadReason::kNoWaitersWhenFinishedStarting:
            a->SleepIfRunning();
            ABSL_FALLTHROUGH_INTENDED;
          case StartThreadReason::kNoWaitersWhenScheduling:
            // Release throttling variable
            GPR_ASSERT(a->state->currently_starting_one_thread.exchange(
                false, std::memory_order_relaxed));
            if (a->state->IsBacklogged()) {
              StartThread(a->state,
                          StartThreadReason::kNoWaitersWhenFinishedStarting);
            }
            break;
        }
        ThreadFunc(a->state);
        // DO NOT SUBMIT(hork): threads will shut down if forking. We must
        // ensure any local callbacks are saved and redistributed. This does not
        // need to be performant.
        GPR_ASSERT(g_local_queue->Empty());
        delete g_local_queue;
      },
      new ThreadState{state, reason}, nullptr,
      grpc_core::Thread::Options().set_tracked(false).set_joinable(false))
      .Start();
}

void ThreadPool::ThreadFunc(ThreadPoolStatePtr state) {
  while (state->Step()) {
  }
  state->thread_count.Remove();
}

bool ThreadPool::ThreadPoolState::Step() {
  // DO NOT SUBMIT(hork): handle forking.
  auto* closure = g_local_queue->PopMostRecent();
  if (closure != nullptr) {
    closure->Run();
    return true;
  }
  grpc_core::ReleasableMutexLock lock(&mu);
  // Wait until work is available or we are shutting down.
  while (!shutdown && !forking && queue.Empty()) {
    // If there are too many threads waiting, then quit this thread.
    // TODO(ctiller): wait some time in this case to be sure.
    if (threads_waiting_ >= reserve_threads_) {
      threads_waiting_++;
      bool timeout = broadcast.WaitWithTimeout(&mu, absl::Seconds(30));
      threads_waiting_--;
      if (timeout && threads_waiting_ >= reserve_threads_) {
        return false;
      }
    } else {
      threads_waiting_++;
      broadcast.Wait(&mu);
      threads_waiting_--;
    }
  }
  if (forking) return false;
  bool qempty = queue.Empty();
  if (shutdown && qempty) return false;
  GPR_ASSERT(!qempty);
  auto callback = queue.PopMostRecent();
  lock.Release();
  if (callback != nullptr) {
    callback->Run();
  }
  return true;
}

ThreadPool::ThreadPool() {
  for (unsigned i = 0; i < state_->reserve_threads_; i++) {
    StartThread(state_, StartThreadReason::kInitialPool);
  }
}

bool ThreadPool::IsThreadPoolThread() {
  return ThreadLocal::IsEventEngineThread();
}

void ThreadPool::Quiesce() {
  SetShutdown(true);
  // Wait until all threads are exited.
  // Note that if this is a threadpool thread then we won't exit this thread
  // until the callstack unwinds a little, so we need to wait for just one
  // thread running instead of zero.
  state_->thread_count.BlockUntilThreadCount(
      ThreadLocal::IsEventEngineThread() ? 1 : 0, "shutting down");
  quiesced_.store(true, std::memory_order_relaxed);
}

ThreadPool::~ThreadPool() {
  GPR_ASSERT(quiesced_.load(std::memory_order_relaxed));
}

void ThreadPool::Run(absl::AnyInvocable<void()> callback) {
  GPR_DEBUG_ASSERT(quiesced_.load(std::memory_order_relaxed) == false);
  // DO NOT SUBMIT(hork): skipping the backlog check may be bad, but the Run
  // path should be fast.
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

bool ThreadPool::ThreadPoolState::IsBacklogged() {
  grpc_core::MutexLock lock(&mu);
  return IsBackloggedLocked();
}

bool ThreadPool::ThreadPoolState::IsBackloggedLocked() {
  if (forking) return false;
  // DO NOT SUBMIT(hork): better heuristic based on
  return queue.Size() > 10;
}

void ThreadPool::SetShutdown(bool is_shutdown) {
  grpc_core::MutexLock lock(&state_->mu);
  auto was_shutdown = std::exchange(state_->shutdown, is_shutdown);
  GPR_ASSERT(is_shutdown != was_shutdown);
  state_->broadcast.SignalAll();
}

void ThreadPool::SetForking(bool is_forking) {
  grpc_core::MutexLock lock(&state_->mu);
  auto was_forking = std::exchange(state_->forking, is_forking);
  GPR_ASSERT(is_forking != was_forking);
  state_->broadcast.SignalAll();
}

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
    // At least once every three seconds (but no faster than once per second in
    // the event of spurious wakeups) log a message indicating we're waiting to
    // fork.
    cv_.WaitWithTimeout(&thread_count_mu_, absl::Seconds(3));
    if (threads_ > threads && absl::Now() - last_log > absl::Seconds(1)) {
      gpr_log(GPR_ERROR, "Waiting for thread pool to idle before %s", why);
      last_log = absl::Now();
    }
  }
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

}  // namespace experimental
}  // namespace grpc_event_engine
