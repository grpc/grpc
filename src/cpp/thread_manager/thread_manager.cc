/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
