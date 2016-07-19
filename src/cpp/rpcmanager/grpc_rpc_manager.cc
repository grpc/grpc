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

#include <grpc++/impl/sync.h>
#include <grpc++/impl/thd.h>
#include <grpc/support/log.h>

#include "src/cpp/rpcmanager/grpc_rpc_manager.h"

namespace grpc {

GrpcRpcManager::GrpcRpcManagerThread::GrpcRpcManagerThread(
    GrpcRpcManager* rpc_mgr)
    : rpc_mgr_(rpc_mgr),
      thd_(new std::thread(&GrpcRpcManager::GrpcRpcManagerThread::Run, this)) {}

void GrpcRpcManager::GrpcRpcManagerThread::Run() {
  rpc_mgr_->MainWorkLoop();
  rpc_mgr_->MarkAsCompleted(this);
}

GrpcRpcManager::GrpcRpcManagerThread::~GrpcRpcManagerThread() {
  thd_->join();
  thd_.reset();
}

GrpcRpcManager::GrpcRpcManager(int min_pollers, int max_pollers,
                               int max_threads)
    : shutdown_(false),
      num_pollers_(0),
      min_pollers_(min_pollers),
      max_pollers_(max_pollers),
      num_threads_(0),
      max_threads_(max_threads) {}

GrpcRpcManager::~GrpcRpcManager() {
  std::unique_lock<grpc::mutex> lock(mu_);

  shutdown_ = true;
  while (num_threads_ != 0) {
    shutdown_cv_.wait(lock);
  }

  CleanupCompletedThreads();
}

// For testing only
void GrpcRpcManager::Wait() {
  std::unique_lock<grpc::mutex> lock(mu_);
  while (!shutdown_) {
    shutdown_cv_.wait(lock);
  }
}

// For testing only
void GrpcRpcManager::ShutdownRpcManager() {
  std::unique_lock<grpc::mutex> lock(mu_);
  shutdown_ = true;
}

void GrpcRpcManager::MarkAsCompleted(GrpcRpcManagerThread* thd) {
  std::unique_lock<grpc::mutex> lock(list_mu_);
  completed_threads_.push_back(thd);
}

void GrpcRpcManager::CleanupCompletedThreads() {
  std::unique_lock<grpc::mutex> lock(list_mu_);
  for (auto thd = completed_threads_.begin(); thd != completed_threads_.end();
       thd = completed_threads_.erase(thd)) {
    delete *thd;
  }
}

void GrpcRpcManager::Initialize() {
  for (int i = 0; i < min_pollers_; i++) {
    MaybeCreatePoller();
  }
}

bool GrpcRpcManager::MaybeContinueAsPoller() {
  std::unique_lock<grpc::mutex> lock(mu_);
  if (shutdown_ || num_pollers_ > max_pollers_ ||
      num_threads_ >= max_threads_) {
    return false;
  }

  num_pollers_++;
  return true;
}

void GrpcRpcManager::MaybeCreatePoller() {
  grpc::unique_lock<grpc::mutex> lock(mu_);
  if (num_pollers_ < min_pollers_ && num_threads_ < max_threads_) {
    num_pollers_++;
    num_threads_++;

    // Create a new thread (which ends up calling the MainWorkLoop() function
    new GrpcRpcManagerThread(this);
  }
}

void GrpcRpcManager::MainWorkLoop() {
  bool is_work_found = false;
  void *tag;

  do {
    PollForWork(is_work_found, &tag);

    // Decrement num_pollers since this thread is no longer polling
    {
      grpc::unique_lock<grpc::mutex> lock(mu_);
      num_pollers_--;
    }

    if (is_work_found) {
      // Start a new poller if needed
      MaybeCreatePoller();

      // Do actual work
      DoWork(tag);
    }

    // Continue to loop if this thread can continue as a poller
  } while (MaybeContinueAsPoller());

  // If we are here, it means that the GrpcRpcManager already has enough threads
  // and that the current thread can be terminated
  {
    grpc::unique_lock<grpc::mutex> lock(mu_);
    num_threads_--;
    if (num_threads_ == 0) {
      shutdown_cv_.notify_all();
    }
  }

  CleanupCompletedThreads();
}

}  // namespace grpc
