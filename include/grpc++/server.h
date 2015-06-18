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

#ifndef GRPCXX_SERVER_H
#define GRPCXX_SERVER_H

#include <list>
#include <memory>

#include <grpc++/completion_queue.h>
#include <grpc++/config.h>
#include <grpc++/impl/call.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc++/impl/service_type.h>
#include <grpc++/impl/sync.h>
#include <grpc++/status.h>

struct grpc_server;

namespace grpc {
class AsynchronousService;
class GenericServerContext;
class AsyncGenericService;
class RpcService;
class RpcServiceMethod;
class ServerCredentials;
class ThreadPoolInterface;

// Currently it only supports handling rpcs in a single thread.
class Server GRPC_FINAL : public GrpcLibrary,
                          private CallHook,
                          private AsynchronousService::DispatchImpl {
 public:
  ~Server();

  // Shutdown the server, block until all rpc processing finishes.
  void Shutdown();

  // Block waiting for all work to complete (the server must either
  // be shutting down or some other thread must call Shutdown for this
  // function to ever return)
  void Wait();

 private:
  friend class AsyncGenericService;
  friend class ServerBuilder;

  class SyncRequest;
  class AsyncRequest;
  class ShutdownRequest;

  // ServerBuilder use only
  Server(ThreadPoolInterface* thread_pool, bool thread_pool_owned,
         int max_message_size);
  // Register a service. This call does not take ownership of the service.
  // The service must exist for the lifetime of the Server instance.
  bool RegisterService(RpcService* service);
  bool RegisterAsyncService(AsynchronousService* service);
  void RegisterAsyncGenericService(AsyncGenericService* service);
  // Add a listening port. Can be called multiple times.
  int AddListeningPort(const grpc::string& addr, ServerCredentials* creds);
  // Start the server.
  bool Start();

  void HandleQueueClosed();
  void RunRpc();
  void ScheduleCallback();

  void PerformOpsOnCall(CallOpBuffer* ops, Call* call) GRPC_OVERRIDE;

  // DispatchImpl
  void RequestAsyncCall(void* registered_method, ServerContext* context,
                        grpc::protobuf::Message* request,
                        ServerAsyncStreamingInterface* stream,
                        CompletionQueue* call_cq,
                        ServerCompletionQueue* notification_cq,
                        void* tag) GRPC_OVERRIDE;

  void RequestAsyncGenericCall(GenericServerContext* context,
                               ServerAsyncStreamingInterface* stream,
                               CompletionQueue* cq,
                               ServerCompletionQueue* notification_cq,
                               void* tag);

  const int max_message_size_;

  // Completion queue.
  CompletionQueue cq_;

  // Sever status
  grpc::mutex mu_;
  bool started_;
  bool shutdown_;
  // The number of threads which are running callbacks.
  int num_running_cb_;
  grpc::condition_variable callback_cv_;

  std::list<SyncRequest>* sync_methods_;

  // Pointer to the c grpc server.
  grpc_server* const server_;

  ThreadPoolInterface* thread_pool_;
  // Whether the thread pool is created and owned by the server.
  bool thread_pool_owned_;
 private:
  Server() : max_message_size_(-1), server_(NULL) { abort(); }
};

}  // namespace grpc

#endif  // GRPCXX_SERVER_H
