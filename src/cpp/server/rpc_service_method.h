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

#ifndef __GRPCPP_INTERNAL_SERVER_RPC_SERVICE_METHOD_H__
#define __GRPCPP_INTERNAL_SERVER_RPC_SERVICE_METHOD_H__

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "src/cpp/rpc_method.h"
#include <google/protobuf/message.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>

namespace grpc {
class ServerContext;
class StreamContextInterface;

// TODO(rocking): we might need to split this file into multiple ones.

// Base class for running an RPC handler.
class MethodHandler {
 public:
  virtual ~MethodHandler() {}
  struct HandlerParameter {
    HandlerParameter(ServerContext* context, const google::protobuf::Message* req,
                     google::protobuf::Message* resp)
        : server_context(context),
          request(req),
          response(resp),
          stream_context(nullptr) {}
    HandlerParameter(ServerContext* context, const google::protobuf::Message* req,
                     google::protobuf::Message* resp, StreamContextInterface* stream)
        : server_context(context),
          request(req),
          response(resp),
          stream_context(stream) {}
    ServerContext* server_context;
    const google::protobuf::Message* request;
    google::protobuf::Message* response;
    StreamContextInterface* stream_context;
  };
  virtual Status RunHandler(const HandlerParameter& param) = 0;
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

  Status RunHandler(const HandlerParameter& param) final {
    // Invoke application function, cast proto messages to their actual types.
    return func_(service_, param.server_context,
                 dynamic_cast<const RequestType*>(param.request),
                 dynamic_cast<ResponseType*>(param.response));
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

  Status RunHandler(const HandlerParameter& param) final {
    ServerReader<RequestType> reader(param.stream_context);
    return func_(service_, param.server_context, &reader,
                 dynamic_cast<ResponseType*>(param.response));
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

  Status RunHandler(const HandlerParameter& param) final {
    ServerWriter<ResponseType> writer(param.stream_context);
    return func_(service_, param.server_context,
                 dynamic_cast<const RequestType*>(param.request), &writer);
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

  Status RunHandler(const HandlerParameter& param) final {
    ServerReaderWriter<ResponseType, RequestType> stream(param.stream_context);
    return func_(service_, param.server_context, &stream);
  }

 private:
  std::function<Status(ServiceType*, ServerContext*,
                       ServerReaderWriter<ResponseType, RequestType>*)> func_;
  ServiceType* service_;
};

// Server side rpc method class
class RpcServiceMethod : public RpcMethod {
 public:
  // Takes ownership of the handler and two prototype objects.
  RpcServiceMethod(const char* name, RpcMethod::RpcType type,
                   MethodHandler* handler, google::protobuf::Message* request_prototype,
                   google::protobuf::Message* response_prototype)
      : RpcMethod(name, type),
        handler_(handler),
        request_prototype_(request_prototype),
        response_prototype_(response_prototype) {}

  MethodHandler* handler() { return handler_.get(); }

  google::protobuf::Message* AllocateRequestProto() { return request_prototype_->New(); }
  google::protobuf::Message* AllocateResponseProto() {
    return response_prototype_->New();
  }

 private:
  std::unique_ptr<MethodHandler> handler_;
  std::unique_ptr<google::protobuf::Message> request_prototype_;
  std::unique_ptr<google::protobuf::Message> response_prototype_;
};

// This class contains all the method information for an rpc service. It is
// used for registering a service on a grpc server.
class RpcService {
 public:
  // Takes ownership.
  void AddMethod(RpcServiceMethod* method) {
    methods_.push_back(std::unique_ptr<RpcServiceMethod>(method));
  }

  RpcServiceMethod* GetMethod(int i) {
    return methods_[i].get();
  }
  int GetMethodCount() const { return methods_.size(); }

 private:
  std::vector<std::unique_ptr<RpcServiceMethod>> methods_;
};

}  // namespace grpc

#endif  // __GRPCPP_INTERNAL_SERVER_RPC_SERVICE_METHOD_H__
