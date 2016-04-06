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

  virtual void SendInitialMetadata(void* tag) = 0;

 private:
  friend class ServerInterface;
  virtual void BindCall(Call* call) = 0;
};

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

 private:
  friend class Server;
  friend class ServerInterface;
  ServerInterface* server_;
  std::vector<std::unique_ptr<RpcServiceMethod>> methods_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_SERVICE_TYPE_H
