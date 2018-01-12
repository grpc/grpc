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

#ifndef GRPC_INTERNAL_CPP_THREAD_MANAGER_H
#define GRPC_INTERNAL_CPP_THREAD_MANAGER_H

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include <grpc++/support/config.h>

namespace grpc {

class ThreadManager {
 public:
  explicit ThreadManager(int min_pollers, int max_pollers);
  virtual ~ThreadManager();

  // Initializes and Starts the Rpc Manager threads
  void Initialize();

  // The return type of PollForWork() function
  enum WorkStatus { WORK_FOUND, SHUTDOWN, TIMEOUT };

  // "Polls" for new work.
  // If the return value is WORK_FOUND:
  //  - The implementaion of PollForWork() MAY set some opaque identifier to
  //    (identify the work item found) via the '*tag' parameter
  //  - The implementaion MUST set the value of 'ok' to 'true' or 'false'. A
  //    value of 'false' indicates some implemenation specific error (that is
  //    neither SHUTDOWN nor TIMEOUT)
  //  - ThreadManager does not interpret the values of 'tag' and 'ok'
  //  - ThreadManager WILL call DoWork() and pass '*tag' and 'ok' as input to
  //    DoWork()
  //
  // If the return value is SHUTDOWN:,
  //  - ThreadManager WILL NOT call DoWork() and terminates the thead
  //
  // If the return value is TIMEOUT:,
  //  - ThreadManager WILL NOT call DoWork()
  //  - ThreadManager MAY terminate the thread depending on the current number
  //    of active poller threads and mix_pollers/max_pollers settings
  //  - Also, the value of timeout is specific to the derived class
  //    implementation
  virtual WorkStatus PollForWork(void** tag, bool* ok) = 0;

  // The implementation of DoWork() is supposed to perform the work found by
  // PollForWork(). The tag and ok parameters are the same as returned by
  // PollForWork()
  //
  // The implementation of DoWork() should also do any setup needed to ensure
  // that the next call to PollForWork() (not necessarily by the current thread)
  // actually finds some work
  virtual void DoWork(void* tag, bool ok) = 0;

  // Mark the ThreadManager as shutdown and begin draining the work. This is a
  // non-blocking call and the caller should call Wait(), a blocking call which
  // returns only once the shutdown is complete
  virtual void Shutdown();

  // Has Shutdown() been called
  bool IsShutdown();

  // A blocking call that returns only after the ThreadManager has shutdown and
  // all the threads have drained all the outstanding work
  virtual void Wait();

 private:
  // Helper wrapper class around std::thread. This takes a ThreadManager object
  // and starts a new std::thread to calls the Run() function.
  //
  // The Run() function calls ThreadManager::MainWorkLoop() function and once
  // that completes, it marks the WorkerThread completed by calling
  // ThreadManager::MarkAsCompleted()
  class WorkerThread {
   public:
    WorkerThread(ThreadManager* thd_mgr);
    ~WorkerThread();

   private:
    // Calls thd_mgr_->MainWorkLoop() and once that completes, calls
    // thd_mgr_>MarkAsCompleted(this) to mark the thread as completed
    void Run();

    ThreadManager* const thd_mgr_;
    std::mutex wt_mu_;
    std::thread thd_;
  };

  // The main funtion in ThreadManager
  void MainWorkLoop();

  void MarkAsCompleted(WorkerThread* thd);
  void CleanupCompletedThreads();

  // Protects shutdown_, num_pollers_ and num_threads_
  // TODO: sreek - Change num_pollers and num_threads_ to atomics
  std::mutex mu_;

  bool shutdown_;
  std::condition_variable shutdown_cv_;

  // Number of threads doing polling
  int num_pollers_;

  // The minimum and maximum number of threads that should be doing polling
  int min_pollers_;
  int max_pollers_;

  // The total number of threads (includes threads includes the threads that are
  // currently polling i.e num_pollers_)
  int num_threads_;

  std::mutex list_mu_;
  std::list<WorkerThread*> completed_threads_;
};

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_THREAD_MANAGER_H
