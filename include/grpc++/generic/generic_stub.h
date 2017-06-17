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

#ifndef GRPCXX_GENERIC_GENERIC_STUB_H
#define GRPCXX_GENERIC_GENERIC_STUB_H

#include <grpc++/support/async_stream.h>
#include <grpc++/support/byte_buffer.h>

namespace grpc {

class CompletionQueue;
typedef ClientAsyncReaderWriter<ByteBuffer, ByteBuffer>
    GenericClientAsyncReaderWriter;

/// Generic stubs provide a type-unsafe interface to call gRPC methods
/// by name.
class GenericStub final {
 public:
  explicit GenericStub(std::shared_ptr<ChannelInterface> channel)
      : channel_(channel) {}

  /// Begin a call to a named method \a method using \a context.
  /// A tag \a tag will be delivered to \a cq when the call has been started
  /// (i.e, initial metadata has been sent).
  /// The return value only indicates whether or not registration of the call
  /// succeeded (i.e. the call won't proceed if the return value is nullptr).
  std::unique_ptr<GenericClientAsyncReaderWriter> Call(
      ClientContext* context, const grpc::string& method, CompletionQueue* cq,
      void* tag);

 private:
  std::shared_ptr<ChannelInterface> channel_;
};

}  // namespace grpc

#endif  // GRPCXX_GENERIC_GENERIC_STUB_H
