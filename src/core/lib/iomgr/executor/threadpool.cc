/*
 *
 * Copyright 2019 gRPC authors.
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

#include "src/core/lib/iomgr/executor/threadpool.h"

namespace grpc_core {

void ThreadPoolWorker::Run() {
  while (true) {
    void* elem;

    if (GRPC_TRACE_FLAG_ENABLED(grpc_thread_pool_trace)) {
      // Updates stats and print
      gpr_timespec wait_time = gpr_time_0(GPR_TIMESPAN);
      elem = queue_->Get(&wait_time);
      stats_.sleep_time = gpr_time_add(stats_.sleep_time, wait_time);
      gpr_log(GPR_INFO,
              "ThreadPool Worker [%s %d] Stats:  sleep_time          %f",
              thd_name_, index_, gpr_timespec_to_micros(stats_.sleep_time));
    } else {
      elem = queue_->Get(nullptr);
    }
    if (elem == nullptr) {
      break;
    }
    // Runs closure
    auto* closure =
        static_cast<grpc_experimental_completion_queue_functor*>(elem);
    closure->functor_run(closure, closure->internal_success);
  }
}

void ThreadPool::SharedThreadPoolConstructor() {
  // All worker threads in thread pool must be joinable.
  thread_options_.set_joinable(true);

  // Create at least 1 worker thread.
  if (num_threads_ <= 0) num_threads_ = 1;

  queue_ = New<InfLenFIFOQueue>();
  threads_ = static_cast<ThreadPoolWorker**>(
      gpr_zalloc(num_threads_ * sizeof(ThreadPoolWorker*)));
  for (int i = 0; i < num_threads_; ++i) {
    threads_[i] =
        New<ThreadPoolWorker>(thd_name_, this, queue_, thread_options_, i);
    threads_[i]->Start();
  }
}

size_t ThreadPool::DefaultStackSize() {
#if defined(__ANDROID__) || defined(__APPLE__)
  return 1952 * 1024;
#else
  return 64 * 1024;
#endif
}

void ThreadPool::AssertHasNotBeenShutDown() {
  // For debug checking purpose, using RELAXED order is sufficient.
  GPR_DEBUG_ASSERT(!shut_down_.Load(MemoryOrder::RELAXED));
}

ThreadPool::ThreadPool(int num_threads) : num_threads_(num_threads) {
  thd_name_ = "ThreadPoolWorker";
  thread_options_ = Thread::Options();
  thread_options_.set_stack_size(DefaultStackSize());
  SharedThreadPoolConstructor();
}

ThreadPool::ThreadPool(int num_threads, const char* thd_name)
    : num_threads_(num_threads), thd_name_(thd_name) {
  thread_options_ = Thread::Options();
  thread_options_.set_stack_size(DefaultStackSize());
  SharedThreadPoolConstructor();
}

ThreadPool::ThreadPool(int num_threads, const char* thd_name,
                       const Thread::Options& thread_options)
    : num_threads_(num_threads),
      thd_name_(thd_name),
      thread_options_(thread_options) {
  if (thread_options_.stack_size() == 0) {
    thread_options_.set_stack_size(DefaultStackSize());
  }
  SharedThreadPoolConstructor();
}

ThreadPool::~ThreadPool() {
  // For debug checking purpose, using RELAXED order is sufficient.
  shut_down_.Store(true, MemoryOrder::RELAXED);

  for (int i = 0; i < num_threads_; ++i) {
    queue_->Put(nullptr);
  }

  for (int i = 0; i < num_threads_; ++i) {
    threads_[i]->Join();
  }

  for (int i = 0; i < num_threads_; ++i) {
    Delete(threads_[i]);
  }
  gpr_free(threads_);
  Delete(queue_);
}

void ThreadPool::Add(grpc_experimental_completion_queue_functor* closure) {
  AssertHasNotBeenShutDown();
  queue_->Put(static_cast<void*>(closure));
}

int ThreadPool::num_pending_closures() const { return queue_->count(); }

int ThreadPool::pool_capacity() const { return num_threads_; }

const Thread::Options& ThreadPool::thread_options() const {
  return thread_options_;
}

const char* ThreadPool::thread_name() const { return thd_name_; }
}  // namespace grpc_core
