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

#include "src/core/lib/gprpp/thd.h"

namespace grpc_event_engine {
namespace experimental {

void ThreadPool::StartThread(std::weak_ptr<Queue> weak_queue,
                             std::shared_ptr<ThreadCount> thread_count) {
  struct Arg {
    std::weak_ptr<Queue> weak_queue;
    std::shared_ptr<ThreadCount> thread_count;
  };
  thread_count->Add();
  grpc_core::Thread(
      "event_engine",
      [](void* arg) {
        auto a = *std::unique_ptr<Arg>(static_cast<Arg*>(arg));
        ThreadFunc(std::move(a.weak_queue), std::move(a.thread_count));
      },
      new Arg{weak_queue, thread_count}, nullptr,
      grpc_core::Thread::Options().set_tracked(false).set_joinable(false))
      .Start();
}

void ThreadPool::ThreadFunc(std::weak_ptr<Queue> weak_queue,
                            std::shared_ptr<ThreadCount> thread_count) {
  if (auto queue = weak_queue.lock()) {
    while (queue->Step()) {
    }
  }
  thread_count->Remove();
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
    StartThread(queue_, thread_count_);
  }
}

ThreadPool::~ThreadPool() { queue_->SetShutdown(); }

void ThreadPool::Add(absl::AnyInvocable<void()> callback) {
  if (queue_->Add(std::move(callback))) {
    StartThread(queue_, thread_count_);
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
}

void ThreadPool::Queue::SetState(State state) {
  grpc_core::MutexLock lock(&mu_);
  GPR_ASSERT(state_ == State::kRunning);
  GPR_ASSERT(state != State::kRunning);
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
  if (threads_ == 0) {
    cv_.Signal();
  }
}

void ThreadPool::ThreadCount::Quiesce() {
  grpc_core::MutexLock lock(&mu_);
  while (threads_ > 0) {
    cv_.Wait(&mu_);
  }
}

void ThreadPool::PrepareFork() {
  queue_->SetForking();
  thread_count_->Quiesce();
  queue_ = nullptr;
}

void ThreadPool::PostforkParent() {
  queue_ = std::make_shared<Queue>(reserve_threads_);
  for (int i = 0; i < reserve_threads_; i++) {
    StartThread(queue_, thread_count_);
  }
}

void ThreadPool::PostforkChild() {
  queue_ = std::make_shared<Queue>(reserve_threads_);
  for (int i = 0; i < reserve_threads_; i++) {
    StartThread(queue_, thread_count_);
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine
