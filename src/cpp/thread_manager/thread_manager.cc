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
#include <grpc/support/thd.h>

namespace grpc {

ThreadManager::WorkerThread::WorkerThread(ThreadManager* thd_mgr, bool* valid,
                                          bool wait)
    : thd_mgr_(thd_mgr), wait_(wait) {
  gpr_thd_options opt = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&opt);

  *valid = valid_ = thd_mgr->thread_creator_(
      &thd_, "worker thread",
      [](void* th) {
        reinterpret_cast<ThreadManager::WorkerThread*>(th)->Run();
      },
      this, &opt);
}

void ThreadManager::WorkerThread::Run() {
  if (wait_) {
    thd_mgr_->MarkAsStarted();
  }
  thd_mgr_->MainWorkLoop();
  thd_mgr_->MarkAsCompleted(this);
}

ThreadManager::WorkerThread::~WorkerThread() {
  // The object will always be fully constructed by now because the destructor
  // is only ever called in two ways:
  //    1. Immediately after an attempted construction when the thread
  //       creation is realized to have failed. In that case, valid_ is false
  //       and an asynchronous thread never ran, so the object was fully
  //       constructed before the destructor was called.
  // OR 2. From the CleanupCompletedThreads function. In that case,
  //       valid_ was true AND the thread function completed AND locked the
  //       thread manager's lock on completion AND either the cleanup function
  //       also grabbed the lock before causing destruction or the thread
  //       manager itself is being destroyed after having gone through a lock
  //       on the call to Wait. So there is a clear happens-before ordering
  //       created through the lock.
  if (valid_) {
    thd_mgr_->thread_joiner_(thd_);
  }
}

ThreadManager::ThreadManager(
    int min_pollers, int max_pollers,
    std::function<int(gpr_thd_id*, const char*, void (*)(void*), void*,
                      const gpr_thd_options*)>
        thread_creator,
    std::function<void(gpr_thd_id)> thread_joiner)
    : shutdown_(false),
      num_pollers_(0),
      min_pollers_(min_pollers),
      max_pollers_(max_pollers == -1 ? INT_MAX : max_pollers),
      num_threads_(0),
      thread_creator_(thread_creator),
      thread_joiner_(thread_joiner) {}

ThreadManager::~ThreadManager() {
  // Do not hold a lock here since threads should all be Wait'ed
  // before reaching destructor
  GPR_ASSERT(num_threads_ == 0);
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
  std::lock_guard<std::mutex> lock(mu_);
  completed_threads_.emplace_back(thd);
  num_threads_--;
  if (num_threads_ == 0) {
    shutdown_cv_.notify_one();
  }
}

void ThreadManager::MarkAsStarted() {
  std::unique_lock<std::mutex> lock(mu_);
  threads_awaited_--;
  if (threads_awaited_ == 0) {
    startup_cv_.notify_all();
  } else {
    startup_cv_.wait(lock, [this] { return (threads_awaited_ == 0); });
  }
}

void ThreadManager::CleanupCompletedThreads() {
  std::vector<std::unique_ptr<WorkerThread>> completed_threads;
  // swap out the completed threads collection
  // and let it get destroyed outside the lock
  {
    std::unique_lock<std::mutex> lock(mu_);
    completed_threads.swap(completed_threads_);
  }
}

void ThreadManager::Initialize() {
  // When called, there are no threads yet in the thread manager
  num_pollers_ = min_pollers_;
  num_threads_ = min_pollers_;
  threads_awaited_ = num_threads_;

  for (int i = 0; i < min_pollers_; i++) {
    // Create a new thread (which ends up calling the MainWorkLoop() function
    bool valid;
    new WorkerThread(this, &valid, true);
    GPR_ASSERT(valid);  // we need to have at least this minimum
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
        bool resources;
        if (!shutdown_ && num_pollers_ < min_pollers_) {
          bool valid;
          // Drop lock before spawning thread to avoid contention
          lock.unlock();
          auto* th = new WorkerThread(this, &valid, false);
          lock.lock();
          if (valid) {
            num_pollers_++;
            num_threads_++;
          } else {
            delete th;
          }
          resources = (num_pollers_ > 0);
        } else {
          resources = true;
        }
        // Drop lock before any application work
        lock.unlock();
        // Lock is always released at this point - do the application work
        DoWork(tag, ok, resources);
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
