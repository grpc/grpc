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

#ifndef GRPCPP_GENERIC_ASYNC_GENERIC_SERVICE_H
#define GRPCPP_GENERIC_ASYNC_GENERIC_SERVICE_H

#include <grpcpp/support/async_stream.h>
#include <grpcpp/support/byte_buffer.h>

namespace grpc {

typedef ServerAsyncReaderWriter<ByteBuffer, ByteBuffer>
    GenericServerAsyncReaderWriter;

/// A generic server context is the same as a regular ServerContext, but also
/// has methods to extract the method and host being used by the generic call
class GenericServerContext final : public ServerContext {
 public:
  const grpc::string& method() const { return method_; }
  const grpc::string& host() const { return host_; }

 private:
  friend class Server;
  friend class ServerInterface;

  grpc::string method_;
  grpc::string host_;
};

/// A generic service at the server side accepts all RPC methods and hosts. It
/// is typically used in proxies. The generic service can be registered to a
/// server which also has other services. Sample usage:
///   ServerBuilder builder;
///   auto cq = builder.AddCompletionQueue();
///   AsyncGenericService generic_service;
///   builder.RegisterAsyncGeneicService(&generic_service);
///   auto server = builder.BuildAndStart();
///
///   // request a new call
///   GenericServerContext context;
///   GenericAsyncReaderWriter stream;
///   generic_service.RequestCall(&context, &stream, cq.get(), cq.get(), tag);
///
/// When tag is retrieved from cq->Next(), context.method() can be used to look
/// at the method and the RPC can be handled accordingly.
class AsyncGenericService final {
 public:
  AsyncGenericService() : server_(nullptr) {}

  /// Request that an incoming RPC be delivered to \a ctx using streaming
  /// object \a reader_writer with streaming call activities on \a call_cq and
  /// call notification on \a notification_cq using \a tag
  void RequestCall(GenericServerContext* ctx,
                   GenericServerAsyncReaderWriter* reader_writer,
                   CompletionQueue* call_cq,
                   ServerCompletionQueue* notification_cq, void* tag);

 private:
  friend class Server;
  Server* server_;
};

}  // namespace grpc

#endif  // GRPCPP_GENERIC_ASYNC_GENERIC_SERVICE_H
