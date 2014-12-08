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

#include <grpc++/server_builder.h>

#include <grpc/support/log.h>
#include <grpc++/server.h>

namespace grpc {

ServerBuilder::ServerBuilder() : thread_pool_(nullptr) {}

void ServerBuilder::RegisterService(RpcService* service) {
  services_.push_back(service);
}

void ServerBuilder::AddPort(const grpc::string& addr) {
  ports_.push_back(addr);
}

void ServerBuilder::SetCredentials(
    const std::shared_ptr<ServerCredentials>& creds) {
  GPR_ASSERT(!creds_);
  creds_ = creds;
}

void ServerBuilder::SetThreadPool(ThreadPoolInterface* thread_pool) {
  thread_pool_ = thread_pool;
}

std::unique_ptr<Server> ServerBuilder::BuildAndStart() {
  std::unique_ptr<Server> server(new Server(thread_pool_, creds_.get()));
  for (auto* service : services_) {
    server->RegisterService(service);
  }
  for (auto& port : ports_) {
    server->AddPort(port);
  }
  server->Start();
  return server;
}

}  // namespace grpc
