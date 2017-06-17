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

#include <grpc++/generic/generic_stub.h>

#include <grpc++/impl/rpc_method.h>

namespace grpc {

// begin a call to a named method
std::unique_ptr<GenericClientAsyncReaderWriter> GenericStub::Call(
    ClientContext* context, const grpc::string& method, CompletionQueue* cq,
    void* tag) {
  return std::unique_ptr<GenericClientAsyncReaderWriter>(
      GenericClientAsyncReaderWriter::Create(
          channel_.get(), cq,
          RpcMethod(method.c_str(), RpcMethod::BIDI_STREAMING), context, tag));
}

}  // namespace grpc
