/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/cpp/thread_manager/thread_manager.h"

#include <climits>
#include <mutex>
#include <thread>

#include <grpc/support/log.h>

namespace grpc {

ThreadManager::WorkerThread::WorkerThread(ThreadManager* thd_mgr)
    : thd_mgr_(thd_mgr), thd_(&ThreadManager::WorkerThread::Run, this) {}

void ThreadManager::WorkerThread::Run() {
  thd_mgr_->MainWorkLoop();
  thd_mgr_->MarkAsCompleted(this);
}

ThreadManager::WorkerThread::~WorkerThread() { thd_.join(); }

ThreadManager::ThreadManager(int min_pollers, int max_pollers)
    : shutdown_(false),
      num_pollers_(0),
      min_pollers_(min_pollers),
      max_pollers_(max_pollers == -1 ? INT_MAX : max_pollers),
      num_threads_(0) {}

ThreadManager::~ThreadManager() {
  {
    std::unique_lock<std::mutex> lock(mu_);
    GPR_ASSERT(num_threads_ == 0);
  }

  CleanupCompletedThreads();
}

void ThreadManager::Wait() {
  std::unique_lock<std::mutex> lock(mu_);
  while (num_threads_ != 0) {
    shutdown_cv_.wait(lock);
  }
}

void ThreadManager::Shutdown() {
  std::unique_lock<std::mutex> lock(mu_);
  shutdown_ = true;
}

bool ThreadManager::IsShutdown() {
  std::unique_lock<std::mutex> lock(mu_);
  return shutdown_;
}

void ThreadManager::MarkAsCompleted(WorkerThread* thd) {
  {
    std::unique_lock<std::mutex> list_lock(list_mu_);
    completed_threads_.push_back(thd);
  }

  std::unique_lock<std::mutex> lock(mu_);
  num_threads_--;
  if (num_threads_ == 0) {
    shutdown_cv_.notify_one();
  }
}

void ThreadManager::CleanupCompletedThreads() {
  std::unique_lock<std::mutex> lock(list_mu_);
  for (auto thd = completed_threads_.begin(); thd != completed_threads_.end();
       thd = completed_threads_.erase(thd)) {
    delete *thd;
  }
}

void ThreadManager::Initialize() {
  for (int i = 0; i < min_pollers_; i++) {
    MaybeCreatePoller();
  }
}

// If the number of pollers (i.e threads currently blocked in PollForWork()) is
// less than max threshold (i.e max_pollers_) and the total number of threads is
// below the maximum threshold, we can let the current thread continue as poller
bool ThreadManager::MaybeContinueAsPoller() {
  std::unique_lock<std::mutex> lock(mu_);
  if (shutdown_ || num_pollers_ > max_pollers_) {
    return false;
  }

  num_pollers_++;
  return true;
}

// Create a new poller if the current number of pollers i.e num_pollers_ (i.e
// threads currently blocked in PollForWork()) is below the threshold (i.e
// min_pollers_) and the total number of threads is below the maximum threshold
void ThreadManager::MaybeCreatePoller() {
  std::unique_lock<std::mutex> lock(mu_);
  if (!shutdown_ && num_pollers_ < min_pollers_) {
    num_pollers_++;
    num_threads_++;

    // Create a new thread (which ends up calling the MainWorkLoop() function
    new WorkerThread(this);
  }
}

void ThreadManager::MainWorkLoop() {
  void* tag;
  bool ok;

  /*
   1. Poll for work (i.e PollForWork())
   2. After returning from PollForWork, reduce the number of pollers by 1. If
      PollForWork() returned a TIMEOUT, then it may indicate that we have more
      polling threads than needed. Check if the number of pollers is greater
      than min_pollers and if so, terminate the thread.
   3. Since we are short of one poller now, see if a new poller has to be
      created (i.e see MaybeCreatePoller() for more details)
   4. Do the actual work (DoWork())
   5. After doing the work, see it this thread can resume polling work (i.e
      see MaybeContinueAsPoller() for more details) */
  do {
    WorkStatus work_status = PollForWork(&tag, &ok);

    {
      std::unique_lock<std::mutex> lock(mu_);
      num_pollers_--;

      if (work_status == TIMEOUT && num_pollers_ > min_pollers_) {
        break;
      }
    }

    // Note that MaybeCreatePoller does check for shutdown and creates a new
    // thread only if ThreadManager is not shutdown
    if (work_status == WORK_FOUND) {
      MaybeCreatePoller();
      DoWork(tag, ok);
    }
  } while (MaybeContinueAsPoller());

  CleanupCompletedThreads();

  // If we are here, either ThreadManager is shutting down or it already has
  // enough threads.
}

}  // namespace grpc
