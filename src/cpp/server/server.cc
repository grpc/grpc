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
#include "src/cpp/server/server_rpc_handler.h"
#include <grpc++/async_server_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/impl/rpc_service_method.h>
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
        grpc_secure_server_create(creds->GetRawCreds(), cq_.cq(), nullptr);
  } else {
    server_ = grpc_server_create(cq_.cq(), nullptr);
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
  for (int i = 0; i < service->GetMethodCount(); ++i) {
    RpcServiceMethod *method = service->GetMethod(i);
    if (method_map_.find(method->name()) != method_map_.end()) {
      gpr_log(GPR_DEBUG, "Attempt to register %s multiple times", method->name());
      return false;
    }
    method_map_.insert(std::make_pair(method->name(), method));
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
  if (thread_pool_) {
    ScheduleCallback();
  }

  return true;
}

void Server::AllowOneRpc() {
  GPR_ASSERT(started_);
  grpc_call_error err = grpc_server_request_call_old(server_, nullptr);
  GPR_ASSERT(err == GRPC_CALL_OK);
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

  // Shutdown the completion queue.
  cq_.Shutdown();
  void *tag = nullptr;
  bool ok = false;
  GPR_ASSERT(false == cq_.Next(&tag, &ok));
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
  void *tag = nullptr;
  AllowOneRpc();
  bool ok = false;
  GPR_ASSERT(cq_.Next(&tag, &ok));
  if (ok) {
    AsyncServerContext *server_context = static_cast<AsyncServerContext *>(tag);
    // server_context could be nullptr during server shutdown.
    if (server_context != nullptr) {
      // Schedule a new callback to handle more rpcs.
      ScheduleCallback();

      RpcServiceMethod *method = nullptr;
      auto iter = method_map_.find(server_context->method());
      if (iter != method_map_.end()) {
        method = iter->second;
      }
      ServerRpcHandler rpc_handler(server_context, method);
      rpc_handler.StartRpc();
    }
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
