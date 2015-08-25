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

#ifndef GRPCXX_IMPL_SERVICE_TYPE_H
#define GRPCXX_IMPL_SERVICE_TYPE_H

#include <grpc++/impl/serialization_traits.h>
#include <grpc++/server.h>
#include <grpc++/support/config.h>
#include <grpc++/support/status.h>

namespace grpc {

class Call;
class CompletionQueue;
class RpcService;
class Server;
class ServerCompletionQueue;
class ServerContext;

class SynchronousService {
 public:
  virtual ~SynchronousService() {}
  virtual RpcService* service() = 0;
};

class ServerAsyncStreamingInterface {
 public:
  virtual ~ServerAsyncStreamingInterface() {}

  virtual void SendInitialMetadata(void* tag) = 0;

 private:
  friend class Server;
  virtual void BindCall(Call* call) = 0;
};

class AsynchronousService {
 public:
  AsynchronousService(const char** method_names, size_t method_count)
      : server_(nullptr),
        method_names_(method_names),
        method_count_(method_count),
        request_args_(nullptr) {}

  ~AsynchronousService() { delete[] request_args_; }

 protected:
  template <class Message>
  void RequestAsyncUnary(int index, ServerContext* context, Message* request,
                         ServerAsyncStreamingInterface* stream,
                         CompletionQueue* call_cq,
                         ServerCompletionQueue* notification_cq, void* tag) {
    server_->RequestAsyncCall(request_args_[index], context, stream, call_cq,
                              notification_cq, tag, request);
  }
  void RequestClientStreaming(int index, ServerContext* context,
                              ServerAsyncStreamingInterface* stream,
                              CompletionQueue* call_cq,
                              ServerCompletionQueue* notification_cq,
                              void* tag) {
    server_->RequestAsyncCall(request_args_[index], context, stream, call_cq,
                              notification_cq, tag);
  }
  template <class Message>
  void RequestServerStreaming(int index, ServerContext* context,
                              Message* request,
                              ServerAsyncStreamingInterface* stream,
                              CompletionQueue* call_cq,
                              ServerCompletionQueue* notification_cq,
                              void* tag) {
    server_->RequestAsyncCall(request_args_[index], context, stream, call_cq,
                              notification_cq, tag, request);
  }
  void RequestBidiStreaming(int index, ServerContext* context,
                            ServerAsyncStreamingInterface* stream,
                            CompletionQueue* call_cq,
                            ServerCompletionQueue* notification_cq, void* tag) {
    server_->RequestAsyncCall(request_args_[index], context, stream, call_cq,
                              notification_cq, tag);
  }

 private:
  friend class Server;
  Server* server_;
  const char** const method_names_;
  size_t method_count_;
  void** request_args_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_SERVICE_TYPE_H
