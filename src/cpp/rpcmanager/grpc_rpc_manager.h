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

#ifndef GRPC_INTERNAL_CPP_GRPC_RPC_MANAGER_H
#define GRPC_INTERNAL_CPP_GRPC_RPC_MANAGER_H

#include <list>
#include <memory>

#include <grpc++/impl/sync.h>
#include <grpc++/impl/thd.h>

namespace grpc {

class GrpcRpcManager {
 public:
  explicit GrpcRpcManager(int min_pollers, int max_pollers);
  virtual ~GrpcRpcManager();

  // This function MUST be called before using the object
  void Initialize();

  enum WorkStatus { WORK_FOUND, SHUTDOWN, TIMEOUT };

  // "Polls" for new work.
  // If the return value is WORK_FOUND:
  //  - The implementaion of PollForWork() MAY set some opaque identifier to
  //    (identify the work item found) via the '*tag' parameter
  //  - The implementaion MUST set the value of 'ok' to 'true' or 'false'. A
  //    value of 'false' indicates some implemenation specific error (that is
  //    neither SHUTDOWN nor TIMEOUT)
  //  - GrpcRpcManager does not interpret the values of 'tag' and 'ok'
  //  - GrpcRpcManager WILL call DoWork() and pass '*tag' and 'ok' as input to
  //    DoWork()
  //
  // If the return value is SHUTDOWN:,
  //  - GrpcManager WILL NOT call DoWork() and terminates the thead
  //
  // If the return value is TIMEOUT:,
  //  - GrpcManager WILL NOT call DoWork()
  //  - GrpcManager MAY terminate the thread depending on the current number of
  //    active poller threads and mix_pollers/max_pollers settings
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

  // Mark the GrpcRpcManager as shutdown and begin draining the work.
  // This is a non-blocking call and the caller should call Wait(), a blocking
  // call which returns only once the shutdown is complete
  void ShutdownRpcManager();

  // Has ShutdownRpcManager() been called
  bool IsShutdown();

  // A blocking call that returns only after the GrpcRpcManager has shutdown and
  // all the threads have drained all the outstanding work
  void Wait();

 private:
  // Helper wrapper class around std::thread. This takes a GrpcRpcManager object
  // and starts a new std::thread to calls the Run() function.
  //
  // The Run() function calls GrpcManager::MainWorkLoop() function and once that
  // completes, it marks the GrpcRpcManagerThread completed by calling
  // GrpcRpcManager::MarkAsCompleted()
  class GrpcRpcManagerThread {
   public:
    GrpcRpcManagerThread(GrpcRpcManager* rpc_mgr);
    ~GrpcRpcManagerThread();

   private:
    // Calls rpc_mgr_->MainWorkLoop() and once that completes, calls
    // rpc_mgr_>MarkAsCompleted(this) to mark the thread as completed
    void Run();

    GrpcRpcManager* rpc_mgr_;
    std::unique_ptr<grpc::thread> thd_;
  };

  // The main funtion in GrpcRpcManager
  void MainWorkLoop();

  // Create a new poller if the number of current pollers is less than the
  // minimum number of pollers needed (i.e min_pollers).
  void MaybeCreatePoller();

  // Returns true if the current thread can resume as a poller. i.e if the
  // current number of pollers is less than the max_pollers.
  bool MaybeContinueAsPoller();

  void MarkAsCompleted(GrpcRpcManagerThread* thd);
  void CleanupCompletedThreads();

  // Protects shutdown_, num_pollers_ and num_threads_
  // TODO: sreek - Change num_pollers and num_threads_ to atomics
  grpc::mutex mu_;

  bool shutdown_;
  grpc::condition_variable shutdown_cv_;

  // Number of threads doing polling
  int num_pollers_;

  // The minimum and maximum number of threads that should be doing polling
  int min_pollers_;
  int max_pollers_;

  // The total number of threads (includes threads includes the threads that are
  // currently polling i.e num_pollers_)
  int num_threads_;

  grpc::mutex list_mu_;
  std::list<GrpcRpcManagerThread*> completed_threads_;
};

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_GRPC_RPC_MANAGER_H
