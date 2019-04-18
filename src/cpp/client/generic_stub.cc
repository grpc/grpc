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

#include <functional>

#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/impl/rpc_method.h>
#include <grpcpp/support/client_callback.h>

namespace grpc_impl {

namespace {
std::unique_ptr<grpc::GenericClientAsyncReaderWriter> CallInternal(
    grpc::ChannelInterface* channel, grpc::ClientContext* context,
    const grpc::string& method, grpc::CompletionQueue* cq, bool start,
    void* tag) {
  return std::unique_ptr<grpc::GenericClientAsyncReaderWriter>(
      grpc::internal::ClientAsyncReaderWriterFactory<grpc::ByteBuffer,
                                                     grpc::ByteBuffer>::
          Create(channel, cq,
                 grpc::internal::RpcMethod(
                     method.c_str(), grpc::internal::RpcMethod::BIDI_STREAMING),
                 context, start, tag));
}

}  // namespace

// begin a call to a named method
std::unique_ptr<grpc::GenericClientAsyncReaderWriter> GenericStub::Call(
    grpc::ClientContext* context, const grpc::string& method,
    grpc::CompletionQueue* cq, void* tag) {
  return CallInternal(channel_.get(), context, method, cq, true, tag);
}

// setup a call to a named method
std::unique_ptr<grpc::GenericClientAsyncReaderWriter> GenericStub::PrepareCall(
    grpc::ClientContext* context, const grpc::string& method,
    grpc::CompletionQueue* cq) {
  return CallInternal(channel_.get(), context, method, cq, false, nullptr);
}

// setup a unary call to a named method
std::unique_ptr<grpc::GenericClientAsyncResponseReader>
GenericStub::PrepareUnaryCall(grpc::ClientContext* context,
                              const grpc::string& method,
                              const grpc::ByteBuffer& request,
                              grpc::CompletionQueue* cq) {
  return std::unique_ptr<grpc::GenericClientAsyncResponseReader>(
      grpc::internal::ClientAsyncResponseReaderFactory<
          grpc::ByteBuffer>::Create(channel_.get(), cq,
                                    grpc::internal::RpcMethod(
                                        method.c_str(),
                                        grpc::internal::RpcMethod::NORMAL_RPC),
                                    context, request, false));
}

void GenericStub::experimental_type::UnaryCall(
    grpc::ClientContext* context, const grpc::string& method,
    const grpc::ByteBuffer* request, grpc::ByteBuffer* response,
    std::function<void(grpc::Status)> on_completion) {
  grpc::internal::CallbackUnaryCall(
      stub_->channel_.get(),
      grpc::internal::RpcMethod(method.c_str(),
                                grpc::internal::RpcMethod::NORMAL_RPC),
      context, request, response, std::move(on_completion));
}

void GenericStub::experimental_type::PrepareBidiStreamingCall(
    grpc::ClientContext* context, const grpc::string& method,
    grpc::experimental::ClientBidiReactor<grpc::ByteBuffer, grpc::ByteBuffer>*
        reactor) {
  grpc::internal::ClientCallbackReaderWriterFactory<
      grpc::ByteBuffer,
      grpc::ByteBuffer>::Create(stub_->channel_.get(),
                                grpc::internal::RpcMethod(
                                    method.c_str(),
                                    grpc::internal::RpcMethod::BIDI_STREAMING),
                                context, reactor);
}

}  // namespace grpc_impl
