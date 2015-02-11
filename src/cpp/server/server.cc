/*
 *
 * Copyright 2014, Google Inc.
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

#include <grpc++/server.h>
#include <utility>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>
#include <grpc++/completion_queue.h>
#include <grpc++/impl/rpc_service_method.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/thread_pool_interface.h>

namespace grpc {

Server::Server(ThreadPoolInterface *thread_pool, bool thread_pool_owned, ServerCredentials *creds)
    : started_(false),
      shutdown_(false),
      num_running_cb_(0),
      thread_pool_(thread_pool),
      thread_pool_owned_(thread_pool_owned),
      secure_(creds != nullptr) {
  if (creds) {
    server_ =
        grpc_secure_server_create(creds->GetRawCreds(), nullptr, nullptr);
  } else {
    server_ = grpc_server_create(nullptr, nullptr);
  }
}

Server::Server() {
  // Should not be called.
  GPR_ASSERT(false);
}

Server::~Server() {
  std::unique_lock<std::mutex> lock(mu_);
  if (started_ && !shutdown_) {
    lock.unlock();
    Shutdown();
  } else {
    lock.unlock();
  }
  grpc_server_destroy(server_);
  if (thread_pool_owned_) {
    delete thread_pool_;
  }
}

bool Server::RegisterService(RpcService *service) {
  if (!cq_sync_) {
    cq_sync_.reset(new CompletionQueue);
  }
  for (int i = 0; i < service->GetMethodCount(); ++i) {
    RpcServiceMethod *method = service->GetMethod(i);
    void *tag = grpc_server_register_method(server_, method->name(), nullptr);
    if (!tag) {
      gpr_log(GPR_DEBUG, "Attempt to register %s multiple times", method->name());
      return false;
    }
    methods_.emplace_back(method, tag);
  }
  return true;
}

int Server::AddPort(const grpc::string &addr) {
  GPR_ASSERT(!started_);
  if (secure_) {
    return grpc_server_add_secure_http2_port(server_, addr.c_str());
  } else {
    return grpc_server_add_http2_port(server_, addr.c_str());
  }
}

bool Server::Start() {
  GPR_ASSERT(!started_);
  started_ = true;
  grpc_server_start(server_);

  // Start processing rpcs.
  if (cq_sync_) {
    for (auto& m : methods_) {
      m.Request(cq_sync_.get());
    }

    ScheduleCallback();
  }

  return true;
}

void Server::Shutdown() {
  {
    std::unique_lock<std::mutex> lock(mu_);
    if (started_ && !shutdown_) {
      shutdown_ = true;
      grpc_server_shutdown(server_);

      // Wait for running callbacks to finish.
      while (num_running_cb_ != 0) {
        callback_cv_.wait(lock);
      }
    }
  }
}

void Server::ScheduleCallback() {
  {
    std::unique_lock<std::mutex> lock(mu_);
    num_running_cb_++;
  }
  thread_pool_->ScheduleCallback(std::bind(&Server::RunRpc, this));
}

void Server::RunRpc() {
  // Wait for one more incoming rpc.
  auto* mrd = MethodRequestData::Wait(cq_sync_.get());
  if (mrd) {
    MethodRequestData::CallData cd(mrd);

    mrd->Request(cq_sync_.get());
    ScheduleCallback();

    cd.Run();
  }

  {
    std::unique_lock<std::mutex> lock(mu_);
    num_running_cb_--;
    if (shutdown_) {
      callback_cv_.notify_all();
    }
  }
}

}  // namespace grpc
