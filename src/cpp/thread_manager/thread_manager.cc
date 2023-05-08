//
//
// Copyright 2016 gRPC authors.
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

#include "src/cpp/thread_manager/thread_manager.h"

#include <climits>
#include <initializer_list>

#include "absl/strings/str_format.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc {

ThreadManager::WorkerThread::WorkerThread(ThreadManager* thd_mgr)
    : thd_mgr_(thd_mgr) {
  // Make thread creation exclusive with respect to its join happening in
  // ~WorkerThread().
  thd_ = grpc_core::Thread(
      "grpcpp_sync_server",
      [](void* th) { static_cast<ThreadManager::WorkerThread*>(th)->Run(); },
      this, &created_);
  if (!created_) {
    gpr_log(GPR_ERROR, "Could not create grpc_sync_server worker-thread");
  }
}

void ThreadManager::WorkerThread::Run() {
  thd_mgr_->MainWorkLoop();
  thd_mgr_->MarkAsCompleted(this);
}

ThreadManager::WorkerThread::~WorkerThread() {
  // Don't join until the thread is fully constructed.
  thd_.Join();
}

ThreadManager::ThreadManager(const char*, grpc_resource_quota* resource_quota,
                             int min_pollers, int max_pollers)
    : shutdown_(false),
      thread_quota_(
          grpc_core::ResourceQuota::FromC(resource_quota)->thread_quota()),
      num_pollers_(0),
      min_pollers_(min_pollers),
      max_pollers_(max_pollers == -1 ? INT_MAX : max_pollers),
      num_threads_(0),
      max_active_threads_sofar_(0) {}

ThreadManager::~ThreadManager() {
  {
    grpc_core::MutexLock lock(&mu_);
    GPR_ASSERT(num_threads_ == 0);
  }

  CleanupCompletedThreads();
}

void ThreadManager::Wait() {
  grpc_core::MutexLock lock(&mu_);
  while (num_threads_ != 0) {
    shutdown_cv_.Wait(&mu_);
  }
}

void ThreadManager::Shutdown() {
  grpc_core::MutexLock lock(&mu_);
  shutdown_ = true;
}

bool ThreadManager::IsShutdown() {
  grpc_core::MutexLock lock(&mu_);
  return shutdown_;
}

int ThreadManager::GetMaxActiveThreadsSoFar() {
  grpc_core::MutexLock list_lock(&list_mu_);
  return max_active_threads_sofar_;
}

void ThreadManager::MarkAsCompleted(WorkerThread* thd) {
  {
    grpc_core::MutexLock list_lock(&list_mu_);
    completed_threads_.push_back(thd);
  }

  {
    grpc_core::MutexLock lock(&mu_);
    num_threads_--;
    if (num_threads_ == 0) {
      shutdown_cv_.Signal();
    }
  }

  // Give a thread back to the resource quota
  thread_quota_->Release(1);
}

void ThreadManager::CleanupCompletedThreads() {
  std::list<WorkerThread*> completed_threads;
  {
    // swap out the completed threads list: allows other threads to clean up
    // more quickly
    grpc_core::MutexLock lock(&list_mu_);
    completed_threads.swap(completed_threads_);
  }
  for (auto thd : completed_threads) delete thd;
}

void ThreadManager::Initialize() {
  if (!thread_quota_->Reserve(min_pollers_)) {
    grpc_core::Crash(absl::StrFormat(
        "No thread quota available to even create the minimum required "
        "polling threads (i.e %d). Unable to start the thread manager",
        min_pollers_));
  }

  {
    grpc_core::MutexLock lock(&mu_);
    num_pollers_ = min_pollers_;
    num_threads_ = min_pollers_;
    max_active_threads_sofar_ = min_pollers_;
  }

  for (int i = 0; i < min_pollers_; i++) {
    WorkerThread* worker = new WorkerThread(this);
    GPR_ASSERT(worker->created());  // Must be able to create the minimum
    worker->Start();
  }
}

void ThreadManager::MainWorkLoop() {
  while (true) {
    void* tag;
    bool ok;
    WorkStatus work_status = PollForWork(&tag, &ok);

    grpc_core::LockableAndReleasableMutexLock lock(&mu_);
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
        // If we got work and there are now insufficient pollers and there is
        // quota available to create a new thread, start a new poller thread
        bool resource_exhausted = false;
        if (!shutdown_ && num_pollers_ < min_pollers_) {
          if (thread_quota_->Reserve(1)) {
            // We can allocate a new poller thread
            num_pollers_++;
            num_threads_++;
            if (num_threads_ > max_active_threads_sofar_) {
              max_active_threads_sofar_ = num_threads_;
            }
            // Drop lock before spawning thread to avoid contention
            lock.Release();
            WorkerThread* worker = new WorkerThread(this);
            if (worker->created()) {
              worker->Start();
            } else {
              // Get lock again to undo changes to poller/thread counters.
              grpc_core::MutexLock failure_lock(&mu_);
              num_pollers_--;
              num_threads_--;
              resource_exhausted = true;
              delete worker;
            }
          } else if (num_pollers_ > 0) {
            // There is still at least some thread polling, so we can go on
            // even though we are below the number of pollers that we would
            // like to have (min_pollers_)
            lock.Release();
          } else {
            // There are no pollers to spare and we couldn't allocate
            // a new thread, so resources are exhausted!
            lock.Release();
            resource_exhausted = true;
          }
        } else {
          // There are a sufficient number of pollers available so we can do
          // the work and continue polling with our existing poller threads
          lock.Release();
        }
        // Lock is always released at this point - do the application work
        // or return resource exhausted if there is new work but we couldn't
        // get a thread in which to do it.
        DoWork(tag, ok, !resource_exhausted);
        // Take the lock again to check post conditions
        lock.Lock();
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

  // This thread is exiting. Do some cleanup work i.e delete already completed
  // worker threads
  CleanupCompletedThreads();

  // If we are here, either ThreadManager is shutting down or it already has
  // enough threads.
}

}  // namespace grpc
