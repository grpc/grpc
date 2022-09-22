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

#include <memory>
#include <utility>

#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/support/log.h>

#include "src/core/lib/gpr/tls.h"
#include "src/core/lib/gprpp/thd.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
// TODO(drfloob): Remove this, and replace it with the WorkQueue* for the
// current thread (with nullptr indicating not a threadpool thread).
GPR_THREAD_LOCAL(bool) g_threadpool_thread;
}  // namespace

void ThreadPool::StartThread(StatePtr state, bool throttled) {
  state->thread_count.Add();
  struct ThreadArg {
    StatePtr state;
    bool throttled;
  };
  grpc_core::Thread(
      "event_engine",
      [](void* arg) {
        std::unique_ptr<ThreadArg> a(static_cast<ThreadArg*>(arg));
        g_threadpool_thread = true;
        if (a->throttled) {
          GPR_ASSERT(a->state->currently_starting_one_thread.exchange(
              false, std::memory_order_relaxed));
        }
        ThreadFunc(a->state);
      },
      new ThreadArg{state, throttled}, nullptr,
      grpc_core::Thread::Options().set_tracked(false).set_joinable(false))
      .Start();
}

void ThreadPool::ThreadFunc(StatePtr state) {
  while (state->queue.Step()) {
  }
  state->thread_count.Remove();
}

bool ThreadPool::Queue::Step() {
  grpc_core::ReleasableMutexLock lock(&mu_);
  // Wait until work is available or we are shutting down.
  while (state_ == State::kRunning && callbacks_.empty()) {
    // If there are too many threads waiting, then quit this thread.
    // TODO(ctiller): wait some time in this case to be sure.
    if (threads_waiting_ >= reserve_threads_) return false;
    threads_waiting_++;
    cv_.Wait(&mu_);
    threads_waiting_--;
  }
  switch (state_) {
    case State::kRunning:
      break;
    case State::kShutdown:
    case State::kForking:
      if (!callbacks_.empty()) break;
      return false;
  }
  GPR_ASSERT(!callbacks_.empty());
  auto callback = std::move(callbacks_.front());
  callbacks_.pop();
  lock.Release();
  callback();
  return true;
}

ThreadPool::ThreadPool(int reserve_threads)
    : reserve_threads_(reserve_threads) {
  for (int i = 0; i < reserve_threads; i++) {
    StartThread(state_, /*throttled=*/false);
  }
}

ThreadPool::~ThreadPool() {
  state_->queue.SetShutdown();
  // Wait until all threads are exited.
  // Note that if this is a threadpool thread then we won't exit this thread
  // until the callstack unwinds a little, so we need to wait for just one
  // thread running instead of zero.
  state_->thread_count.BlockUntilThreadCount(g_threadpool_thread ? 1 : 0,
                                             "shutting down");
}

void ThreadPool::Add(absl::AnyInvocable<void()> callback) {
  if (state_->queue.Add(std::move(callback))) {
    if (!state_->currently_starting_one_thread.exchange(
            true, std::memory_order_relaxed)) {
      StartThread(state_, /*throttled=*/true);
    }
  }
}

bool ThreadPool::Queue::Add(absl::AnyInvocable<void()> callback) {
  grpc_core::MutexLock lock(&mu_);
  // Add works to the callbacks list
  callbacks_.push(std::move(callback));
  cv_.Signal();
  switch (state_) {
    case State::kRunning:
    case State::kShutdown:
      return threads_waiting_ == 0;
    case State::kForking:
      return false;
  }
  GPR_UNREACHABLE_CODE(return false);
}

void ThreadPool::Queue::SetState(State state) {
  grpc_core::MutexLock lock(&mu_);
  if (state == State::kRunning) {
    GPR_ASSERT(state_ != State::kRunning);
  } else {
    GPR_ASSERT(state_ == State::kRunning);
  }
  state_ = state;
  cv_.SignalAll();
}

void ThreadPool::ThreadCount::Add() {
  grpc_core::MutexLock lock(&mu_);
  ++threads_;
}

void ThreadPool::ThreadCount::Remove() {
  grpc_core::MutexLock lock(&mu_);
  --threads_;
  cv_.Signal();
}

void ThreadPool::ThreadCount::BlockUntilThreadCount(int threads,
                                                    const char* why) {
  grpc_core::MutexLock lock(&mu_);
  auto last_log = absl::Now();
  while (threads_ > threads) {
    // Wait for all threads to exit.
    // At least once every three seconds (but no faster than once per second in
    // the event of spurious wakeups) log a message indicating we're waiting to
    // fork.
    cv_.WaitWithTimeout(&mu_, absl::Seconds(3));
    if (threads_ > threads && absl::Now() - last_log > absl::Seconds(1)) {
      gpr_log(GPR_ERROR, "Waiting for thread pool to idle before %s", why);
      last_log = absl::Now();
    }
  }
}

void ThreadPool::PrepareFork() {
  state_->queue.SetForking();
  state_->thread_count.BlockUntilThreadCount(0, "forking");
}

void ThreadPool::PostforkParent() { Postfork(); }

void ThreadPool::PostforkChild() { Postfork(); }

void ThreadPool::Postfork() {
  state_->queue.Reset();
  for (int i = 0; i < reserve_threads_; i++) {
    StartThread(state_, /*throttled=*/false);
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine
