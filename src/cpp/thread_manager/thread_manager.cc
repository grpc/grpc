/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/cpp/thread_manager/thread_manager.h"

#include <climits>
#include <mutex>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/thd.h"

namespace grpc {

ThreadManager::WorkerThread::WorkerThread(ThreadManager* thd_mgr)
    : thd_mgr_(thd_mgr) {
  // Make thread creation exclusive with respect to its join happening in
  // ~WorkerThread().
  thd_ = grpc_core::Thread(
      "grpcpp_sync_server",
      [](void* th) { static_cast<ThreadManager::WorkerThread*>(th)->Run(); },
      this);
  thd_.Start();
}

void ThreadManager::WorkerThread::Run() {
  thd_mgr_->MainWorkLoop();
  thd_mgr_->MarkAsCompleted(this);
}

ThreadManager::WorkerThread::~WorkerThread() {
  // Don't join until the thread is fully constructed.
  thd_.Join();
}

ThreadManager::ThreadManager(int min_pollers, int max_pollers)
    : shutdown_(false),
      num_pollers_(0),
      min_pollers_(min_pollers),
      max_pollers_(max_pollers == -1 ? INT_MAX : max_pollers),
      num_threads_(0) {}

ThreadManager::~ThreadManager() {
  {
    std::lock_guard<std::mutex> lock(mu_);
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
  std::lock_guard<std::mutex> lock(mu_);
  shutdown_ = true;
}

bool ThreadManager::IsShutdown() {
  std::lock_guard<std::mutex> lock(mu_);
  return shutdown_;
}

void ThreadManager::MarkAsCompleted(WorkerThread* thd) {
  {
    std::lock_guard<std::mutex> list_lock(list_mu_);
    completed_threads_.push_back(thd);
  }

  std::lock_guard<std::mutex> lock(mu_);
  num_threads_--;
  if (num_threads_ == 0) {
    shutdown_cv_.notify_one();
  }
}

void ThreadManager::CleanupCompletedThreads() {
  std::list<WorkerThread*> completed_threads;
  {
    // swap out the completed threads list: allows other threads to clean up
    // more quickly
    std::unique_lock<std::mutex> lock(list_mu_);
    completed_threads.swap(completed_threads_);
  }
  for (auto thd : completed_threads) delete thd;
}

void ThreadManager::Initialize() {
  {
    std::unique_lock<std::mutex> lock(mu_);
    num_pollers_ = min_pollers_;
    num_threads_ = min_pollers_;
  }

  for (int i = 0; i < min_pollers_; i++) {
    // Create a new thread (which ends up calling the MainWorkLoop() function
    new WorkerThread(this);
  }
}

void ThreadManager::MainWorkLoop() {
  while (true) {
    void* tag;
    bool ok;
    WorkStatus work_status = PollForWork(&tag, &ok);

    std::unique_lock<std::mutex> lock(mu_);
    // Reduce the number of pollers by 1 and check what happened with the poll
    num_pollers_--;
    bool done = false;
    switch (work_status) {
      case TIMEOUT:
        // If we timed out and we have more pollers than we need (or we are
        // shutdown), finish this thread
        if (shutdown_ || num_pollers_ > max_pollers_) done = true;
        break;
      case SHUTDOWN:
        // If the thread manager is shutdown, finish this thread
        done = true;
        break;
      case WORK_FOUND:
        // If we got work and there are now insufficient pollers, start a new
        // one
        if (!shutdown_ && num_pollers_ < min_pollers_) {
          num_pollers_++;
          num_threads_++;
          // Drop lock before spawning thread to avoid contention
          lock.unlock();
          new WorkerThread(this);
        } else {
          // Drop lock for consistency with above branch
          lock.unlock();
        }
        // Lock is always released at this point - do the application work
        DoWork(tag, ok);
        // Take the lock again to check post conditions
        lock.lock();
        // If we're shutdown, we should finish at this point.
        if (shutdown_) done = true;
        break;
    }
    // If we decided to finish the thread, break out of the while loop
    if (done) break;

    // Otherwise go back to polling as long as it doesn't exceed max_pollers_
    //
    // **WARNING**:
    // There is a possibility of threads thrashing here (i.e excessive thread
    // shutdowns and creations than the ideal case). This happens if max_poller_
    // count is small and the rate of incoming requests is also small. In such
    // scenarios we can possibly configure max_pollers_ to a higher value and/or
    // increase the cq timeout.
    //
    // However, not doing this check here and unconditionally incrementing
    // num_pollers (and hoping that the system will eventually settle down) has
    // far worse consequences i.e huge number of threads getting created to the
    // point of thread-exhaustion. For example: if the incoming request rate is
    // very high, all the polling threads will return very quickly from
    // PollForWork() with WORK_FOUND. They all briefly decrement num_pollers_
    // counter thereby possibly - and briefly - making it go below min_pollers;
    // This will most likely result in the creation of a new poller since
    // num_pollers_ dipped below min_pollers_.
    //
    // Now, If we didn't do the max_poller_ check here, all these threads will
    // go back to doing PollForWork() and the whole cycle repeats (with a new
    // thread being added in each cycle). Once the total number of threads in
    // the system crosses a certain threshold (around ~1500), there is heavy
    // contention on mutexes (the mu_ here or the mutexes in gRPC core like the
    // pollset mutex) that makes DoWork() take longer to finish thereby causing
    // new poller threads to be created even faster. This results in a thread
    // avalanche.
    if (num_pollers_ < max_pollers_) {
      num_pollers_++;
    } else {
      break;
    }
  };

  CleanupCompletedThreads();

  // If we are here, either ThreadManager is shutting down or it already has
  // enough threads.
}

}  // namespace grpc
