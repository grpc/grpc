//
//
// Copyright 2018 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#ifndef GRPCPP_GENERIC_ASYNC_GENERIC_SERVICE_H
#define GRPCPP_GENERIC_ASYNC_GENERIC_SERVICE_H

#include <grpc/support/port_platform.h>

#include <grpcpp/impl/server_callback_handlers.h>
#include <grpcpp/support/async_stream.h>
#include <grpcpp/support/byte_buffer.h>
#include <grpcpp/support/server_callback.h>

struct grpc_server;

namespace grpc {

typedef ServerAsyncReaderWriter<ByteBuffer, ByteBuffer>
    GenericServerAsyncReaderWriter;
typedef ServerAsyncResponseWriter<ByteBuffer> GenericServerAsyncResponseWriter;
typedef ServerAsyncReader<ByteBuffer, ByteBuffer> GenericServerAsyncReader;
typedef ServerAsyncWriter<ByteBuffer> GenericServerAsyncWriter;

class GenericServerContext final : public ServerContext {
 public:
  const std::string& method() const { return method_; }
  const std::string& host() const { return host_; }

 private:
  friend class ServerInterface;

  std::string method_;
  std::string host_;
};

// A generic service at the server side accepts all RPC methods and hosts. It is
// typically used in proxies. The generic service can be registered to a server
// which also has other services.
// Sample usage:
//   ServerBuilder builder;
//   auto cq = builder.AddCompletionQueue();
//   AsyncGenericService generic_service;
//   builder.RegisterAsyncGenericService(&generic_service);
//   auto server = builder.BuildAndStart();
//
//   // request a new call
//   GenericServerContext context;
//   GenericServerAsyncReaderWriter stream;
//   generic_service.RequestCall(&context, &stream, cq.get(), cq.get(), tag);
//
// When tag is retrieved from cq->Next(), context.method() can be used to look
// at the method and the RPC can be handled accordingly.
class AsyncGenericService final {
 public:
  AsyncGenericService() : server_(nullptr) {}

  void RequestCall(GenericServerContext* ctx,
                   GenericServerAsyncReaderWriter* reader_writer,
                   grpc::CompletionQueue* call_cq,
                   grpc::ServerCompletionQueue* notification_cq, void* tag);

 private:
  friend class grpc::Server;
  grpc::Server* server_;
};

/// \a ServerGenericBidiReactor is the reactor class for bidi streaming RPCs
/// invoked on a CallbackGenericService. It is just a ServerBidi reactor with
/// ByteBuffer arguments.
using ServerGenericBidiReactor = ServerBidiReactor<ByteBuffer, ByteBuffer>;

class GenericCallbackServerContext final : public grpc::CallbackServerContext {
 public:
  const std::string& method() const { return method_; }
  const std::string& host() const { return host_; }

 private:
  friend class grpc::Server;

  std::string method_;
  std::string host_;
};

/// \a CallbackGenericService is the base class for generic services implemented
/// using the callback API and registered through the ServerBuilder using
/// RegisterCallbackGenericService.
class CallbackGenericService {
 public:
  CallbackGenericService() {}
  virtual ~CallbackGenericService() {}

  /// The "method handler" for the generic API. This function should be
  /// overridden to provide a ServerGenericBidiReactor that implements the
  /// application-level interface for this RPC. Unimplemented by default.
  virtual ServerGenericBidiReactor* CreateReactor(
      GenericCallbackServerContext* /*ctx*/) {
    class Reactor : public ServerGenericBidiReactor {
     public:
      Reactor() { this->Finish(Status(StatusCode::UNIMPLEMENTED, "")); }
      void OnDone() override { delete this; }
    };
    return new Reactor;
  }

 private:
  friend class grpc::Server;

  internal::CallbackBidiHandler<ByteBuffer, ByteBuffer>* Handler() {
    return new internal::CallbackBidiHandler<ByteBuffer, ByteBuffer>(
        [this](grpc::CallbackServerContext* ctx) {
          return CreateReactor(static_cast<GenericCallbackServerContext*>(ctx));
        });
  }

  grpc::Server* server_{nullptr};
};

}  // namespace grpc

#endif  // GRPCPP_GENERIC_ASYNC_GENERIC_SERVICE_H
