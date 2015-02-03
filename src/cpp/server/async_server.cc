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

#include <grpc++/async_server.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc++/completion_queue.h>

namespace grpc {

AsyncServer::AsyncServer(CompletionQueue *cc)
    : started_(false), shutdown_(false) {
  server_ = grpc_server_create(cc->cq(), nullptr);
}

AsyncServer::~AsyncServer() {
  std::unique_lock<std::mutex> lock(shutdown_mu_);
  if (started_ && !shutdown_) {
    lock.unlock();
    Shutdown();
  }
  grpc_server_destroy(server_);
}

void AsyncServer::AddPort(const grpc::string &addr) {
  GPR_ASSERT(!started_);
  int success = grpc_server_add_http2_port(server_, addr.c_str());
  GPR_ASSERT(success);
}

void AsyncServer::Start() {
  GPR_ASSERT(!started_);
  started_ = true;
  grpc_server_start(server_);
}

void AsyncServer::RequestOneRpc() {
  GPR_ASSERT(started_);
  std::unique_lock<std::mutex> lock(shutdown_mu_);
  if (shutdown_) {
    return;
  }
  lock.unlock();
  grpc_call_error err = grpc_server_request_call_old(server_, nullptr);
  GPR_ASSERT(err == GRPC_CALL_OK);
}

void AsyncServer::Shutdown() {
  std::unique_lock<std::mutex> lock(shutdown_mu_);
  if (started_ && !shutdown_) {
    shutdown_ = true;
    lock.unlock();
    // TODO(yangg) should we shutdown without start?
    grpc_server_shutdown(server_);
  }
}

}  // namespace grpc
