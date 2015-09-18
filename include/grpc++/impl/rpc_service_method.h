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

#ifndef GRPCXX_IMPL_RPC_SERVICE_METHOD_H
#define GRPCXX_IMPL_RPC_SERVICE_METHOD_H

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <grpc++/impl/rpc_method.h>
#include <grpc++/support/config.h>
#include <grpc++/support/status.h>
#include <grpc++/support/sync_stream.h>

namespace grpc {
class ServerContext;
class StreamContextInterface;

// TODO(rocking): we might need to split this file into multiple ones.

// Base class for running an RPC handler.
class MethodHandler {
 public:
  virtual ~MethodHandler() {}
  struct HandlerParameter {
    HandlerParameter(Call* c, ServerContext* context, grpc_byte_buffer* req,
                     int max_size)
        : call(c),
          server_context(context),
          request(req),
          max_message_size(max_size) {}
    Call* call;
    ServerContext* server_context;
    // Handler required to grpc_byte_buffer_destroy this
    grpc_byte_buffer* request;
    int max_message_size;
  };
  virtual void RunHandler(const HandlerParameter& param) = 0;
};

// A wrapper class of an application provided rpc method handler.
template <class ServiceType, class RequestType, class ResponseType>
class RpcMethodHandler : public MethodHandler {
 public:
  RpcMethodHandler(
      std::function<Status(ServiceType*, ServerContext*, const RequestType*,
                           ResponseType*)> func,
      ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) GRPC_FINAL {
    RequestType req;
    Status status = SerializationTraits<RequestType>::Deserialize(
        param.request, &req, param.max_message_size);
    ResponseType rsp;
    if (status.ok()) {
      status = func_(service_, param.server_context, &req, &rsp);
    }

    GPR_ASSERT(!param.server_context->sent_initial_metadata_);
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpServerSendStatus> ops;
    ops.SendInitialMetadata(param.server_context->initial_metadata_);
    if (status.ok()) {
      status = ops.SendMessage(rsp);
    }
    ops.ServerSendStatus(param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }

 private:
  // Application provided rpc handler function.
  std::function<Status(ServiceType*, ServerContext*, const RequestType*,
                       ResponseType*)> func_;
  // The class the above handler function lives in.
  ServiceType* service_;
};

// A wrapper class of an application provided client streaming handler.
template <class ServiceType, class RequestType, class ResponseType>
class ClientStreamingHandler : public MethodHandler {
 public:
  ClientStreamingHandler(
      std::function<Status(ServiceType*, ServerContext*,
                           ServerReader<RequestType>*, ResponseType*)> func,
      ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) GRPC_FINAL {
    ServerReader<RequestType> reader(param.call, param.server_context);
    ResponseType rsp;
    Status status = func_(service_, param.server_context, &reader, &rsp);

    GPR_ASSERT(!param.server_context->sent_initial_metadata_);
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpServerSendStatus> ops;
    ops.SendInitialMetadata(param.server_context->initial_metadata_);
    if (status.ok()) {
      status = ops.SendMessage(rsp);
    }
    ops.ServerSendStatus(param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }

 private:
  std::function<Status(ServiceType*, ServerContext*, ServerReader<RequestType>*,
                       ResponseType*)> func_;
  ServiceType* service_;
};

// A wrapper class of an application provided server streaming handler.
template <class ServiceType, class RequestType, class ResponseType>
class ServerStreamingHandler : public MethodHandler {
 public:
  ServerStreamingHandler(
      std::function<Status(ServiceType*, ServerContext*, const RequestType*,
                           ServerWriter<ResponseType>*)> func,
      ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) GRPC_FINAL {
    RequestType req;
    Status status = SerializationTraits<RequestType>::Deserialize(
        param.request, &req, param.max_message_size);

    if (status.ok()) {
      ServerWriter<ResponseType> writer(param.call, param.server_context);
      status = func_(service_, param.server_context, &req, &writer);
    }

    CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> ops;
    if (!param.server_context->sent_initial_metadata_) {
      ops.SendInitialMetadata(param.server_context->initial_metadata_);
    }
    ops.ServerSendStatus(param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }

 private:
  std::function<Status(ServiceType*, ServerContext*, const RequestType*,
                       ServerWriter<ResponseType>*)> func_;
  ServiceType* service_;
};

// A wrapper class of an application provided bidi-streaming handler.
template <class ServiceType, class RequestType, class ResponseType>
class BidiStreamingHandler : public MethodHandler {
 public:
  BidiStreamingHandler(
      std::function<Status(ServiceType*, ServerContext*,
                           ServerReaderWriter<ResponseType, RequestType>*)>
          func,
      ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) GRPC_FINAL {
    ServerReaderWriter<ResponseType, RequestType> stream(param.call,
                                                         param.server_context);
    Status status = func_(service_, param.server_context, &stream);

    CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> ops;
    if (!param.server_context->sent_initial_metadata_) {
      ops.SendInitialMetadata(param.server_context->initial_metadata_);
    }
    ops.ServerSendStatus(param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }

 private:
  std::function<Status(ServiceType*, ServerContext*,
                       ServerReaderWriter<ResponseType, RequestType>*)> func_;
  ServiceType* service_;
};

// Handle unknown method by returning UNIMPLEMENTED error.
class UnknownMethodHandler : public MethodHandler {
 public:
  template <class T>
  static void FillOps(ServerContext* context, T* ops) {
    Status status(StatusCode::UNIMPLEMENTED, "");
    if (!context->sent_initial_metadata_) {
      ops->SendInitialMetadata(context->initial_metadata_);
      context->sent_initial_metadata_ = true;
    }
    ops->ServerSendStatus(context->trailing_metadata_, status);
  }

  void RunHandler(const HandlerParameter& param) GRPC_FINAL {
    CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> ops;
    FillOps(param.server_context, &ops);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }
};

// Server side rpc method class
class RpcServiceMethod : public RpcMethod {
 public:
  // Takes ownership of the handler
  RpcServiceMethod(const char* name, RpcMethod::RpcType type,
                   MethodHandler* handler)
      : RpcMethod(name, type), handler_(handler) {}

  MethodHandler* handler() { return handler_.get(); }

 private:
  std::unique_ptr<MethodHandler> handler_;
};

// This class contains all the method information for an rpc service. It is
// used for registering a service on a grpc server.
class RpcService {
 public:
  // Takes ownership.
  void AddMethod(RpcServiceMethod* method) { methods_.emplace_back(method); }

  RpcServiceMethod* GetMethod(int i) { return methods_[i].get(); }
  int GetMethodCount() const { return methods_.size(); }

 private:
  std::vector<std::unique_ptr<RpcServiceMethod>> methods_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_RPC_SERVICE_METHOD_H
