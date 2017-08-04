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

#include "src/cpp/server/dynamic_thread_pool.h"

#include <mutex>
#include <thread>

#include <grpc/support/log.h>

namespace grpc {

DynamicThreadPool::DynamicThread::DynamicThread(DynamicThreadPool* pool)
    : pool_(pool),
      thd_(new std::thread(&DynamicThreadPool::DynamicThread::ThreadFunc,
                           this)) {}
DynamicThreadPool::DynamicThread::~DynamicThread() {
  thd_->join();
  thd_.reset();
}

void DynamicThreadPool::DynamicThread::ThreadFunc() {
  pool_->ThreadFunc();
  // Now that we have killed ourselves, we should reduce the thread count
  std::unique_lock<std::mutex> lock(pool_->mu_);
  pool_->nthreads_--;
  // Move ourselves to dead list
  pool_->dead_threads_.push_back(this);

  if ((pool_->shutdown_) && (pool_->nthreads_ == 0)) {
    pool_->shutdown_cv_.notify_one();
  }
}

void DynamicThreadPool::ThreadFunc() {
  for (;;) {
    // Wait until work is available or we are shutting down.
    std::unique_lock<std::mutex> lock(mu_);
    if (!shutdown_ && callbacks_.empty()) {
      // If there are too many threads waiting, then quit this thread
      if (threads_waiting_ >= reserve_threads_) {
        break;
      }
      threads_waiting_++;
      cv_.wait(lock);
      threads_waiting_--;
    }
    // Drain callbacks before considering shutdown to ensure all work
    // gets completed.
    if (!callbacks_.empty()) {
      auto cb = callbacks_.front();
      callbacks_.pop();
      lock.unlock();
      cb();
    } else if (shutdown_) {
      break;
    }
  }
}

DynamicThreadPool::DynamicThreadPool(int reserve_threads)
    : shutdown_(false),
      reserve_threads_(reserve_threads),
      nthreads_(0),
      threads_waiting_(0) {
  for (int i = 0; i < reserve_threads_; i++) {
    std::lock_guard<std::mutex> lock(mu_);
    nthreads_++;
    new DynamicThread(this);
  }
}

void DynamicThreadPool::ReapThreads(std::list<DynamicThread*>* tlist) {
  for (auto t = tlist->begin(); t != tlist->end(); t = tlist->erase(t)) {
    delete *t;
  }
}

DynamicThreadPool::~DynamicThreadPool() {
  std::unique_lock<std::mutex> lock(mu_);
  shutdown_ = true;
  cv_.notify_all();
  while (nthreads_ != 0) {
    shutdown_cv_.wait(lock);
  }
  ReapThreads(&dead_threads_);
}

void DynamicThreadPool::Add(const std::function<void()>& callback) {
  std::lock_guard<std::mutex> lock(mu_);
  // Add works to the callbacks list
  callbacks_.push(callback);
  // Increase pool size or notify as needed
  if (threads_waiting_ == 0) {
    // Kick off a new thread
    nthreads_++;
    new DynamicThread(this);
  } else {
    cv_.notify_one();
  }
  // Also use this chance to harvest dead threads
  if (!dead_threads_.empty()) {
    ReapThreads(&dead_threads_);
  }
}

}  // namespace grpc
