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

#ifndef GRPCXX_IMPL_CODEGEN_SERVER_INTERFACE_H
#define GRPCXX_IMPL_CODEGEN_SERVER_INTERFACE_H

#include <grpc++/impl/codegen/call_hook.h>
#include <grpc++/impl/codegen/completion_queue_tag.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/rpc_service_method.h>
#include <grpc/impl/codegen/grpc_types.h>

namespace grpc {

class AsyncGenericService;
class GenericServerContext;
class RpcService;
class ServerAsyncStreamingInterface;
class ServerCompletionQueue;
class ServerContext;
class ServerCredentials;
class Service;
class ThreadPoolInterface;

extern CoreCodegenInterface* g_core_codegen_interface;

/// Models a gRPC server.
///
/// Servers are configured and started via \a grpc::ServerBuilder.
class ServerInterface : public CallHook {
 public:
  virtual ~ServerInterface() {}

  /// Shutdown the server, blocking until all rpc processing finishes.
  /// Forcefully terminate pending calls after \a deadline expires.
  ///
  /// All completion queue associated with the server (for example, for async
  /// serving) must be shutdown *after* this method has returned:
  /// See \a ServerBuilder::AddCompletionQueue for details.
  ///
  /// \param deadline How long to wait until pending rpcs are forcefully
  /// terminated.
  template <class T>
  void Shutdown(const T& deadline) {
    ShutdownInternal(TimePoint<T>(deadline).raw_time());
  }

  /// Shutdown the server, waiting for all rpc processing to finish.
  ///
  /// All completion queue associated with the server (for example, for async
  /// serving) must be shutdown *after* this method has returned:
  /// See \a ServerBuilder::AddCompletionQueue for details.
  void Shutdown() {
    ShutdownInternal(
        g_core_codegen_interface->gpr_inf_future(GPR_CLOCK_MONOTONIC));
  }

  /// Block waiting for all work to complete.
  ///
  /// \warning The server must be either shutting down or some other thread must
  /// call \a Shutdown for this function to ever return.
  virtual void Wait() = 0;

 protected:
  friend class Service;

  /// Register a service. This call does not take ownership of the service.
  /// The service must exist for the lifetime of the Server instance.
  virtual bool RegisterService(const grpc::string* host, Service* service) = 0;

  /// Register a generic service. This call does not take ownership of the
  /// service. The service must exist for the lifetime of the Server instance.
  virtual void RegisterAsyncGenericService(AsyncGenericService* service) = 0;

  /// Tries to bind \a server to the given \a addr.
  ///
  /// It can be invoked multiple times.
  ///
  /// \param addr The address to try to bind to the server (eg, localhost:1234,
  /// 192.168.1.1:31416, [::1]:27182, etc.).
  /// \params creds The credentials associated with the server.
  ///
  /// \return bound port number on sucess, 0 on failure.
  ///
  /// \warning It's an error to call this method on an already started server.
  virtual int AddListeningPort(const grpc::string& addr,
                               ServerCredentials* creds) = 0;

  /// Start the server.
  ///
  /// \param cqs Completion queues for handling asynchronous services. The
  /// caller is required to keep all completion queues live until the server is
  /// destroyed.
  /// \param num_cqs How many completion queues does \a cqs hold.
  ///
  /// \return true on a successful shutdown.
  virtual bool Start(ServerCompletionQueue** cqs, size_t num_cqs) = 0;

  virtual void ShutdownInternal(gpr_timespec deadline) = 0;

  virtual int max_receive_message_size() const = 0;

  virtual grpc_server* server() = 0;

  virtual void PerformOpsOnCall(CallOpSetInterface* ops, Call* call) = 0;

  class BaseAsyncRequest : public CompletionQueueTag {
   public:
    BaseAsyncRequest(ServerInterface* server, ServerContext* context,
                     ServerAsyncStreamingInterface* stream,
                     CompletionQueue* call_cq, void* tag,
                     bool delete_on_finalize);
    virtual ~BaseAsyncRequest();

    bool FinalizeResult(void** tag, bool* status) override;

   protected:
    ServerInterface* const server_;
    ServerContext* const context_;
    ServerAsyncStreamingInterface* const stream_;
    CompletionQueue* const call_cq_;
    void* const tag_;
    const bool delete_on_finalize_;
    grpc_call* call_;
  };

  class RegisteredAsyncRequest : public BaseAsyncRequest {
   public:
    RegisteredAsyncRequest(ServerInterface* server, ServerContext* context,
                           ServerAsyncStreamingInterface* stream,
                           CompletionQueue* call_cq, void* tag);

    // uses BaseAsyncRequest::FinalizeResult

   protected:
    void IssueRequest(void* registered_method, grpc_byte_buffer** payload,
                      ServerCompletionQueue* notification_cq);
  };

  class NoPayloadAsyncRequest final : public RegisteredAsyncRequest {
   public:
    NoPayloadAsyncRequest(void* registered_method, ServerInterface* server,
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
  class PayloadAsyncRequest final : public RegisteredAsyncRequest {
   public:
    PayloadAsyncRequest(void* registered_method, ServerInterface* server,
                        ServerContext* context,
                        ServerAsyncStreamingInterface* stream,
                        CompletionQueue* call_cq,
                        ServerCompletionQueue* notification_cq, void* tag,
                        Message* request)
        : RegisteredAsyncRequest(server, context, stream, call_cq, tag),
          request_(request) {
      IssueRequest(registered_method, &payload_, notification_cq);
    }

    bool FinalizeResult(void** tag, bool* status) override {
      bool serialization_status =
          *status && payload_ &&
          SerializationTraits<Message>::Deserialize(payload_, request_).ok();
      bool ret = RegisteredAsyncRequest::FinalizeResult(tag, status);
      *status = serialization_status && *status;
      return ret;
    }

   private:
    grpc_byte_buffer* payload_;
    Message* const request_;
  };

  class GenericAsyncRequest : public BaseAsyncRequest {
   public:
    GenericAsyncRequest(ServerInterface* server, GenericServerContext* context,
                        ServerAsyncStreamingInterface* stream,
                        CompletionQueue* call_cq,
                        ServerCompletionQueue* notification_cq, void* tag,
                        bool delete_on_finalize);

    bool FinalizeResult(void** tag, bool* status) override;

   private:
    grpc_call_details call_details_;
  };

  template <class Message>
  void RequestAsyncCall(RpcServiceMethod* method, ServerContext* context,
                        ServerAsyncStreamingInterface* stream,
                        CompletionQueue* call_cq,
                        ServerCompletionQueue* notification_cq, void* tag,
                        Message* message) {
    GPR_CODEGEN_ASSERT(method);
    new PayloadAsyncRequest<Message>(method->server_tag(), this, context,
                                     stream, call_cq, notification_cq, tag,
                                     message);
  }

  void RequestAsyncCall(RpcServiceMethod* method, ServerContext* context,
                        ServerAsyncStreamingInterface* stream,
                        CompletionQueue* call_cq,
                        ServerCompletionQueue* notification_cq, void* tag) {
    GPR_CODEGEN_ASSERT(method);
    new NoPayloadAsyncRequest(method->server_tag(), this, context, stream,
                              call_cq, notification_cq, tag);
  }

  void RequestAsyncGenericCall(GenericServerContext* context,
                               ServerAsyncStreamingInterface* stream,
                               CompletionQueue* call_cq,
                               ServerCompletionQueue* notification_cq,
                               void* tag) {
    new GenericAsyncRequest(this, context, stream, call_cq, notification_cq,
                            tag, true);
  }
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_SERVER_INTERFACE_H
