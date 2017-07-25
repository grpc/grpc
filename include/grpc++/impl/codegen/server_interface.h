/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
class Channel;
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
  virtual void Start(ServerCompletionQueue** cqs, size_t num_cqs) = 0;

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
          registered_method_(registered_method),
          server_(server),
          context_(context),
          stream_(stream),
          call_cq_(call_cq),
          notification_cq_(notification_cq),
          tag_(tag),
          request_(request) {
      IssueRequest(registered_method, &payload_, notification_cq);
    }

    bool FinalizeResult(void** tag, bool* status) override {
      if (*status) {
        if (payload_ == nullptr ||
            !SerializationTraits<Message>::Deserialize(payload_, request_)
                 .ok()) {
          // If deserialization fails, we cancel the call and instantiate
          // a new instance of ourselves to request another call.  We then
          // return false, which prevents the call from being returned to
          // the application.
          g_core_codegen_interface->grpc_call_cancel_with_status(
              call_, GRPC_STATUS_INTERNAL, "Unable to parse request", nullptr);
          g_core_codegen_interface->grpc_call_unref(call_);
          new PayloadAsyncRequest(registered_method_, server_, context_,
                                  stream_, call_cq_, notification_cq_, tag_,
                                  request_);
          delete this;
          return false;
        }
      }
      return RegisteredAsyncRequest::FinalizeResult(tag, status);
    }

   private:
    void* const registered_method_;
    ServerInterface* const server_;
    ServerContext* const context_;
    ServerAsyncStreamingInterface* const stream_;
    CompletionQueue* const call_cq_;
    ServerCompletionQueue* const notification_cq_;
    void* const tag_;
    Message* const request_;
    grpc_byte_buffer* payload_;
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
