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

#include <utility>

#include "src/core/lib/gprpp/thd.h"

namespace grpc_event_engine {
namespace experimental {

ThreadPool::Thread::Thread(ThreadPool* pool)
    : pool_(pool),
      thd_(
          "posix_eventengine_pool",
          [](void* th) { static_cast<ThreadPool::Thread*>(th)->ThreadFunc(); },
          this, nullptr, grpc_core::Thread::Options().set_tracked(false)) {
  thd_.Start();
}
ThreadPool::Thread::~Thread() { thd_.Join(); }

void ThreadPool::Thread::ThreadFunc() {
  pool_->ThreadFunc();
  // Now that we have killed ourselves, we should reduce the thread count
  grpc_core::MutexLock lock(&pool_->mu_);
  pool_->nthreads_--;
  // Move ourselves to dead list
  pool_->dead_threads_.push_back(this);

  if (pool_->nthreads_ == 0) {
    if (pool_->forking_) pool_->fork_cv_.Signal();
    if (pool_->shutdown_) pool_->shutdown_cv_.Signal();
  }
}

void ThreadPool::ThreadFunc() {
  for (;;) {
    // Wait until work is available or we are shutting down.
    grpc_core::ReleasableMutexLock lock(&mu_);
    if (!forking_ && !shutdown_ && callbacks_.empty()) {
      // If there are too many threads waiting, then quit this thread
      if (threads_waiting_ >= reserve_threads_) {
        break;
      }
      threads_waiting_++;
      cv_.Wait(&mu_);
      threads_waiting_--;
    }
    // a fork could be initiated while the thread was waiting
    if (forking_) return;
    // Drain callbacks before considering shutdown to ensure all work
    // gets completed.
    if (!callbacks_.empty()) {
      auto cb = std::move(callbacks_.front());
      callbacks_.pop();
      lock.Release();
      cb();
    } else if (shutdown_) {
      break;
    }
  }
}

ThreadPool::ThreadPool(int reserve_threads)
    : shutdown_(false),
      reserve_threads_(reserve_threads),
      nthreads_(0),
      threads_waiting_(0),
      forking_(false) {
  grpc_core::MutexLock lock(&mu_);
  StartNThreadsLocked(reserve_threads_);
}

void ThreadPool::StartNThreadsLocked(int n) {
  for (int i = 0; i < n; i++) {
    nthreads_++;
    new Thread(this);
  }
}

void ThreadPool::ReapThreads(std::vector<Thread*>* tlist) {
  for (auto* t : *tlist) delete t;
  tlist->clear();
}

ThreadPool::~ThreadPool() {
  grpc_core::MutexLock lock(&mu_);
  shutdown_ = true;
  cv_.SignalAll();
  while (nthreads_ != 0) {
    shutdown_cv_.Wait(&mu_);
  }
  ReapThreads(&dead_threads_);
}

void ThreadPool::Add(absl::AnyInvocable<void()> callback) {
  grpc_core::MutexLock lock(&mu_);
  // Add works to the callbacks list
  callbacks_.push(std::move(callback));
  // Store the callback for later if we are forking.
  // TODO(hork): should we block instead?
  if (forking_) return;
  // Increase pool size or notify as needed
  if (threads_waiting_ == 0) {
    // Kick off a new thread
    nthreads_++;
    new Thread(this);
  } else {
    cv_.Signal();
  }
  // Also use this chance to harvest dead threads
  if (!dead_threads_.empty()) {
    ReapThreads(&dead_threads_);
  }
}

void ThreadPool::PrepareFork() {
  grpc_core::MutexLock lock(&mu_);
  forking_ = true;
  cv_.SignalAll();
  while (nthreads_ != 0) {
    fork_cv_.Wait(&mu_);
  }
  ReapThreads(&dead_threads_);
}

void ThreadPool::PostforkParent() {
  grpc_core::MutexLock lock(&mu_);
  forking_ = false;
  StartNThreadsLocked(reserve_threads_);
}

void ThreadPool::PostforkChild() {
  grpc_core::MutexLock lock(&mu_);
  forking_ = false;
  StartNThreadsLocked(reserve_threads_);
}

}  // namespace experimental
}  // namespace grpc_event_engine
