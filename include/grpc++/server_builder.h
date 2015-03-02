/*
 *
 * Copyright 2015, Google Inc.
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

#ifndef GRPCXX_SERVER_BUILDER_H
#define GRPCXX_SERVER_BUILDER_H

#include <memory>
#include <vector>

#include <grpc++/config.h>

namespace grpc {

class AsynchronousService;
class CompletionQueue;
class RpcService;
class Server;
class ServerCredentials;
class SynchronousService;
class ThreadPoolInterface;

class ServerBuilder {
 public:
  ServerBuilder();

  // Register a service. This call does not take ownership of the service.
  // The service must exist for the lifetime of the Server instance returned by
  // BuildAndStart().
  void RegisterService(SynchronousService* service);

  // Register an asynchronous service. New calls will be delevered to cq.
  // This call does not take ownership of the service or completion queue.
  // The service and completion queuemust exist for the lifetime of the Server
  // instance returned by BuildAndStart().
  void RegisterAsyncService(AsynchronousService* service);

  // Add a listening port. Can be called multiple times.
  void AddPort(const grpc::string& addr);

  // Set a ServerCredentials. Can only be called once.
  // TODO(yangg) move this to be part of AddPort
  void SetCredentials(const std::shared_ptr<ServerCredentials>& creds);

  // Set the thread pool used for running appliation rpc handlers.
  // Does not take ownership.
  void SetThreadPool(ThreadPoolInterface* thread_pool);

  // Return a running server which is ready for processing rpcs.
  std::unique_ptr<Server> BuildAndStart();

 private:
  std::vector<RpcService*> services_;
  std::vector<AsynchronousService*> async_services_;
  std::vector<grpc::string> ports_;
  std::shared_ptr<ServerCredentials> creds_;
  ThreadPoolInterface* thread_pool_;
};

}  // namespace grpc

#endif  // GRPCXX_SERVER_BUILDER_H
