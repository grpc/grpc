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

#ifndef GRPCXX_IMPL_CODEGEN_SERVICE_TYPE_H
#define GRPCXX_IMPL_CODEGEN_SERVICE_TYPE_H

#include <grpc++/impl/codegen/config.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/rpc_service_method.h>
#include <grpc++/impl/codegen/serialization_traits.h>
#include <grpc++/impl/codegen/server_interface.h>
#include <grpc++/impl/codegen/status.h>

namespace grpc {

class Call;
class CompletionQueue;
class Server;
class ServerInterface;
class ServerCompletionQueue;
class ServerContext;

class ServerAsyncStreamingInterface {
 public:
  virtual ~ServerAsyncStreamingInterface() {}

  /// Request notification of the sending of initial metadata to the client.
  /// Completion will be notified by \a tag on the associated completion
  /// queue. This call is optional, but if it is used, it cannot be used
  /// concurrently with or after the \a Finish method.
  ///
  /// \param[in] tag Tag identifying this request.
  virtual void SendInitialMetadata(void* tag) = 0;

 private:
  friend class ServerInterface;
  virtual void BindCall(Call* call) = 0;
};

/// Desriptor of an RPC service and its various RPC methods
class Service {
 public:
  Service() : server_(nullptr) {}
  virtual ~Service() {}

  bool has_async_methods() const {
    for (auto it = methods_.begin(); it != methods_.end(); ++it) {
      if (*it && (*it)->handler() == nullptr) {
        return true;
      }
    }
    return false;
  }

  bool has_synchronous_methods() const {
    for (auto it = methods_.begin(); it != methods_.end(); ++it) {
      if (*it && (*it)->handler() != nullptr) {
        return true;
      }
    }
    return false;
  }

  bool has_generic_methods() const {
    for (auto it = methods_.begin(); it != methods_.end(); ++it) {
      if (it->get() == nullptr) {
        return true;
      }
    }
    return false;
  }

 protected:
  template <class Message>
  void RequestAsyncUnary(int index, ServerContext* context, Message* request,
                         ServerAsyncStreamingInterface* stream,
                         CompletionQueue* call_cq,
                         ServerCompletionQueue* notification_cq, void* tag) {
    server_->RequestAsyncCall(methods_[index].get(), context, stream, call_cq,
                              notification_cq, tag, request);
  }
  void RequestAsyncClientStreaming(int index, ServerContext* context,
                                   ServerAsyncStreamingInterface* stream,
                                   CompletionQueue* call_cq,
                                   ServerCompletionQueue* notification_cq,
                                   void* tag) {
    server_->RequestAsyncCall(methods_[index].get(), context, stream, call_cq,
                              notification_cq, tag);
  }
  template <class Message>
  void RequestAsyncServerStreaming(int index, ServerContext* context,
                                   Message* request,
                                   ServerAsyncStreamingInterface* stream,
                                   CompletionQueue* call_cq,
                                   ServerCompletionQueue* notification_cq,
                                   void* tag) {
    server_->RequestAsyncCall(methods_[index].get(), context, stream, call_cq,
                              notification_cq, tag, request);
  }
  void RequestAsyncBidiStreaming(int index, ServerContext* context,
                                 ServerAsyncStreamingInterface* stream,
                                 CompletionQueue* call_cq,
                                 ServerCompletionQueue* notification_cq,
                                 void* tag) {
    server_->RequestAsyncCall(methods_[index].get(), context, stream, call_cq,
                              notification_cq, tag);
  }

  void AddMethod(RpcServiceMethod* method) { methods_.emplace_back(method); }

  void MarkMethodAsync(int index) {
    GPR_CODEGEN_ASSERT(
        methods_[index].get() != nullptr &&
        "Cannot mark the method as 'async' because it has already been "
        "marked as 'generic'.");
    methods_[index]->ResetHandler();
  }

  void MarkMethodGeneric(int index) {
    GPR_CODEGEN_ASSERT(
        methods_[index]->handler() != nullptr &&
        "Cannot mark the method as 'generic' because it has already been "
        "marked as 'async'.");
    methods_[index].reset();
  }

  void MarkMethodStreamed(int index, MethodHandler* streamed_method) {
    GPR_CODEGEN_ASSERT(methods_[index] && methods_[index]->handler() &&
                       "Cannot mark an async or generic method Streamed");
    methods_[index]->SetHandler(streamed_method);

    // From the server's point of view, streamed unary is a special
    // case of BIDI_STREAMING that has 1 read and 1 write, in that order,
    // and split server-side streaming is BIDI_STREAMING with 1 read and
    // any number of writes, in that order.
    methods_[index]->SetMethodType(::grpc::RpcMethod::BIDI_STREAMING);
  }

 private:
  friend class Server;
  friend class ServerInterface;
  ServerInterface* server_;
  std::vector<std::unique_ptr<RpcServiceMethod>> methods_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_SERVICE_TYPE_H
