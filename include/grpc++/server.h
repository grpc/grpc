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
#include <grpc++/impl/sync.h>
#include <grpc++/status.h>

struct grpc_server;

namespace grpc {

class AsynchronousService;
class GenericServerContext;
class AsyncGenericService;
class RpcService;
class RpcServiceMethod;
class ServerAsyncStreamingInterface;
class ServerCredentials;
class ThreadPoolInterface;

// Currently it only supports handling rpcs in a single thread.
class Server GRPC_FINAL : public GrpcLibrary, private CallHook {
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
  friend class AsynchronousService;
  friend class ServerBuilder;

  class SyncRequest;
  class AsyncRequest;
  class ShutdownRequest;

  // ServerBuilder use only
  Server(ThreadPoolInterface* thread_pool, bool thread_pool_owned,
         int max_message_size);
  // Register a service. This call does not take ownership of the service.
  // The service must exist for the lifetime of the Server instance.
  bool RegisterService(const grpc::string *host, RpcService* service);
  bool RegisterAsyncService(const grpc::string *host, AsynchronousService* service);
  void RegisterAsyncGenericService(AsyncGenericService* service);
  // Add a listening port. Can be called multiple times.
  int AddListeningPort(const grpc::string& addr, ServerCredentials* creds);
  // Start the server.
  bool Start();

  void HandleQueueClosed();
  void RunRpc();
  void ScheduleCallback();

  void PerformOpsOnCall(CallOpSetInterface* ops, Call* call) GRPC_OVERRIDE;

  class BaseAsyncRequest : public CompletionQueueTag {
   public:
    BaseAsyncRequest(Server* server, ServerContext* context,
                     ServerAsyncStreamingInterface* stream,
                     CompletionQueue* call_cq, void* tag);
    virtual ~BaseAsyncRequest();

    bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE;

   protected:
    Server* const server_;
    ServerContext* const context_;
    ServerAsyncStreamingInterface* const stream_;
    CompletionQueue* const call_cq_;
    void* const tag_;
    grpc_call* call_;
    grpc_metadata_array initial_metadata_array_;
  };

  class RegisteredAsyncRequest : public BaseAsyncRequest {
   public:
    RegisteredAsyncRequest(Server* server, ServerContext* context,
                           ServerAsyncStreamingInterface* stream,
                           CompletionQueue* call_cq, void* tag);

    // uses BaseAsyncRequest::FinalizeResult

   protected:
    void IssueRequest(void* registered_method, grpc_byte_buffer** payload,
                      ServerCompletionQueue* notification_cq);
  };

  class NoPayloadAsyncRequest GRPC_FINAL : public RegisteredAsyncRequest {
   public:
    NoPayloadAsyncRequest(void* registered_method, Server* server,
                          ServerContext* context,
                          ServerAsyncStreamingInterface* stream,
                          CompletionQueue* call_cq,
                          ServerCompletionQueue* notification_cq, void* tag)
        : RegisteredAsyncRequest(server, context, stream, call_cq, tag) {
      IssueRequest(registered_method, nullptr, notification_cq);
    }

    // uses RegisteredAsyncRequest::FinalizeResult
  };

  template <class Message>
  class PayloadAsyncRequest GRPC_FINAL : public RegisteredAsyncRequest {
   public:
    PayloadAsyncRequest(void* registered_method, Server* server,
                        ServerContext* context,
                        ServerAsyncStreamingInterface* stream,
                        CompletionQueue* call_cq,
                        ServerCompletionQueue* notification_cq, void* tag,
                        Message* request)
        : RegisteredAsyncRequest(server, context, stream, call_cq, tag),
          request_(request) {
      IssueRequest(registered_method, &payload_, notification_cq);
    }

    bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE {
      bool serialization_status =
          *status && payload_ &&
          SerializationTraits<Message>::Deserialize(payload_, request_,
                                                    server_->max_message_size_)
              .ok();
      bool ret = RegisteredAsyncRequest::FinalizeResult(tag, status);
      *status = serialization_status && *status;
      return ret;
    }

   private:
    grpc_byte_buffer* payload_;
    Message* const request_;
  };

  class GenericAsyncRequest GRPC_FINAL : public BaseAsyncRequest {
   public:
    GenericAsyncRequest(Server* server, GenericServerContext* context,
                        ServerAsyncStreamingInterface* stream,
                        CompletionQueue* call_cq,
                        ServerCompletionQueue* notification_cq, void* tag);

    bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE;

   private:
    grpc_call_details call_details_;
  };

  template <class Message>
  void RequestAsyncCall(void* registered_method, ServerContext* context,
                        ServerAsyncStreamingInterface* stream,
                        CompletionQueue* call_cq,
                        ServerCompletionQueue* notification_cq, void* tag,
                        Message* message) {
    new PayloadAsyncRequest<Message>(registered_method, this, context, stream,
                                     call_cq, notification_cq, tag, message);
  }

  void RequestAsyncCall(void* registered_method, ServerContext* context,
                        ServerAsyncStreamingInterface* stream,
                        CompletionQueue* call_cq,
                        ServerCompletionQueue* notification_cq, void* tag) {
    new NoPayloadAsyncRequest(registered_method, this, context, stream, call_cq,
                              notification_cq, tag);
  }

  void RequestAsyncGenericCall(GenericServerContext* context,
                               ServerAsyncStreamingInterface* stream,
                               CompletionQueue* call_cq,
                               ServerCompletionQueue* notification_cq,
                               void* tag) {
    new GenericAsyncRequest(this, context, stream, call_cq, notification_cq,
                            tag);
  }

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
};

}  // namespace grpc

#endif  // GRPCXX_SERVER_H
