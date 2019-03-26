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

#ifndef GRPCPP_IMPL_CODEGEN_ASYNC_GENERIC_SERVICE_H
#define GRPCPP_IMPL_CODEGEN_ASYNC_GENERIC_SERVICE_H

#include <grpcpp/impl/codegen/async_stream.h>
#include <grpcpp/impl/codegen/byte_buffer.h>
#include <grpcpp/impl/codegen/server_callback.h>

struct grpc_server;

namespace grpc {

typedef ServerAsyncReaderWriter<ByteBuffer, ByteBuffer>
    GenericServerAsyncReaderWriter;
typedef ServerAsyncResponseWriter<ByteBuffer> GenericServerAsyncResponseWriter;
typedef ServerAsyncReader<ByteBuffer, ByteBuffer> GenericServerAsyncReader;
typedef ServerAsyncWriter<ByteBuffer> GenericServerAsyncWriter;

class GenericServerContext final : public ServerContext {
 public:
  const grpc::string& method() const { return method_; }
  const grpc::string& host() const { return host_; }

 private:
  friend class Server;
  friend class ServerInterface;

  void Clear() {
    method_.clear();
    host_.clear();
    ServerContext::Clear();
  }

  grpc::string method_;
  grpc::string host_;
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
                   CompletionQueue* call_cq,
                   ServerCompletionQueue* notification_cq, void* tag);

 private:
  friend class Server;
  Server* server_;
};

namespace experimental {

class ServerGenericBidiReactor
    : public ServerBidiReactor<ByteBuffer, ByteBuffer> {
 public:
  void OnStarted(ServerContext* ctx) final {
    OnStarted(static_cast<GenericServerContext*>(ctx));
  }
  virtual void OnStarted(GenericServerContext* ctx) {}
};

}  // namespace experimental

namespace internal {
class UnimplementedGenericBidiReactor
    : public experimental::ServerGenericBidiReactor {
 public:
  void OnDone() override { delete this; }
  void OnStarted(GenericServerContext*) override {
    this->Finish(Status(StatusCode::UNIMPLEMENTED, ""));
  }
};
}  // namespace internal

namespace experimental {
class CallbackGenericService {
 public:
  CallbackGenericService() {}
  virtual ~CallbackGenericService() {}
  virtual ServerGenericBidiReactor* CreateReactor() {
    return new internal::UnimplementedGenericBidiReactor;
  }

 private:
  friend class ::grpc::Server;

  internal::CallbackBidiHandler<ByteBuffer, ByteBuffer>* Handler() {
    return new internal::CallbackBidiHandler<ByteBuffer, ByteBuffer>(
        [this] { return CreateReactor(); });
  }

  Server* server_{nullptr};
};
}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_ASYNC_GENERIC_SERVICE_H
