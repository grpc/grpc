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

#ifndef GRPCPP_GENERIC_GENERIC_STUB_H
#define GRPCPP_GENERIC_GENERIC_STUB_H

#include <functional>

#include <grpcpp/support/async_stream.h>
#include <grpcpp/support/async_unary_call.h>
#include <grpcpp/support/byte_buffer.h>
#include <grpcpp/support/client_callback.h>
#include <grpcpp/support/status.h>

namespace grpc {

class CompletionQueue;
typedef ClientAsyncReaderWriter<ByteBuffer, ByteBuffer>
    GenericClientAsyncReaderWriter;
typedef ClientAsyncResponseReader<ByteBuffer> GenericClientAsyncResponseReader;

/// Generic stubs provide a type-unsafe interface to call gRPC methods
/// by name.
class GenericStub final {
 public:
  explicit GenericStub(std::shared_ptr<ChannelInterface> channel)
      : channel_(channel) {}

  /// Setup a call to a named method \a method using \a context, but don't
  /// start it. Let it be started explicitly with StartCall and a tag.
  /// The return value only indicates whether or not registration of the call
  /// succeeded (i.e. the call won't proceed if the return value is nullptr).
  std::unique_ptr<GenericClientAsyncReaderWriter> PrepareCall(
      ClientContext* context, const grpc::string& method, CompletionQueue* cq);

  /// Setup a unary call to a named method \a method using \a context, and don't
  /// start it. Let it be started explicitly with StartCall.
  /// The return value only indicates whether or not registration of the call
  /// succeeded (i.e. the call won't proceed if the return value is nullptr).
  std::unique_ptr<GenericClientAsyncResponseReader> PrepareUnaryCall(
      ClientContext* context, const grpc::string& method,
      const ByteBuffer& request, CompletionQueue* cq);

  /// DEPRECATED for multi-threaded use
  /// Begin a call to a named method \a method using \a context.
  /// A tag \a tag will be delivered to \a cq when the call has been started
  /// (i.e, initial metadata has been sent).
  /// The return value only indicates whether or not registration of the call
  /// succeeded (i.e. the call won't proceed if the return value is nullptr).
  std::unique_ptr<GenericClientAsyncReaderWriter> Call(
      ClientContext* context, const grpc::string& method, CompletionQueue* cq,
      void* tag);

  /// NOTE: class experimental_type is not part of the public API of this class
  /// TODO(vjpai): Move these contents to the public API of GenericStub when
  ///              they are no longer experimental
  class experimental_type {
   public:
    explicit experimental_type(GenericStub* stub) : stub_(stub) {}

    void UnaryCall(ClientContext* context, const grpc::string& method,
                   const ByteBuffer* request, ByteBuffer* response,
                   std::function<void(Status)> on_completion);

    void PrepareBidiStreamingCall(
        ClientContext* context, const grpc::string& method,
        experimental::ClientBidiReactor<ByteBuffer, ByteBuffer>* reactor);

   private:
    GenericStub* stub_;
  };

  /// NOTE: The function experimental() is not stable public API. It is a view
  /// to the experimental components of this class. It may be changed or removed
  /// at any time.
  experimental_type experimental() { return experimental_type(this); }

 private:
  std::shared_ptr<ChannelInterface> channel_;
};

}  // namespace grpc

#endif  // GRPCPP_GENERIC_GENERIC_STUB_H
