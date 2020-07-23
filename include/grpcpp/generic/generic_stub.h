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

#include <grpcpp/client_context.h>
#include <grpcpp/impl/rpc_method.h>
#include <grpcpp/support/async_stream_impl.h>
#include <grpcpp/support/async_unary_call_impl.h>
#include <grpcpp/support/byte_buffer.h>
#include <grpcpp/support/client_callback_impl.h>
#include <grpcpp/support/status.h>

namespace grpc_impl {
class CompletionQueue;
}  // namespace grpc_impl
namespace grpc {

typedef ::grpc_impl::ClientAsyncReaderWriter<ByteBuffer, ByteBuffer>
    GenericClientAsyncReaderWriter;
typedef ::grpc_impl::ClientAsyncResponseReader<ByteBuffer>
    GenericClientAsyncResponseReader;

/// Generic stubs provide a type-unaware interface to call gRPC methods
/// by name. In practice, the Request and Response types should be basic
/// types like grpc::ByteBuffer or proto::MessageLite (the base protobuf).
template <class RequestType, class ResponseType>
class TemplatedGenericStub final {
 public:
  explicit TemplatedGenericStub(std::shared_ptr<grpc::ChannelInterface> channel)
      : channel_(channel) {}

  /// Setup a call to a named method \a method using \a context, but don't
  /// start it. Let it be started explicitly with StartCall and a tag.
  /// The return value only indicates whether or not registration of the call
  /// succeeded (i.e. the call won't proceed if the return value is nullptr).
  std::unique_ptr<
      ::grpc_impl::ClientAsyncReaderWriter<RequestType, ResponseType>>
  PrepareCall(ClientContext* context, const std::string& method,
              ::grpc_impl::CompletionQueue* cq) {
    return CallInternal(channel_.get(), context, method, cq, false, nullptr);
  }

  /// Setup a unary call to a named method \a method using \a context, and don't
  /// start it. Let it be started explicitly with StartCall.
  /// The return value only indicates whether or not registration of the call
  /// succeeded (i.e. the call won't proceed if the return value is nullptr).
  std::unique_ptr<::grpc_impl::ClientAsyncResponseReader<ResponseType>>
  PrepareUnaryCall(ClientContext* context, const std::string& method,
                   const RequestType& request,
                   ::grpc_impl::CompletionQueue* cq) {
    return std::unique_ptr<
        ::grpc_impl::ClientAsyncResponseReader<ResponseType>>(
        grpc_impl::internal::ClientAsyncResponseReaderFactory<
            ResponseType>::Create(channel_.get(), cq,
                                  grpc::internal::RpcMethod(
                                      method.c_str(),
                                      grpc::internal::RpcMethod::NORMAL_RPC),
                                  context, request, false));
  }

  /// DEPRECATED for multi-threaded use
  /// Begin a call to a named method \a method using \a context.
  /// A tag \a tag will be delivered to \a cq when the call has been started
  /// (i.e, initial metadata has been sent).
  /// The return value only indicates whether or not registration of the call
  /// succeeded (i.e. the call won't proceed if the return value is nullptr).
  std::unique_ptr<
      ::grpc_impl::ClientAsyncReaderWriter<RequestType, ResponseType>>
  Call(ClientContext* context, const std::string& method,
       ::grpc_impl::CompletionQueue* cq, void* tag) {
    return CallInternal(channel_.get(), context, method, cq, true, tag);
  }

#ifdef GRPC_CALLBACK_API_NONEXPERIMENTAL
  /// Setup and start a unary call to a named method \a method using
  /// \a context and specifying the \a request and \a response buffers.
  void UnaryCall(ClientContext* context, const std::string& method,
                 const RequestType* request, ResponseType* response,
                 std::function<void(grpc::Status)> on_completion) {
    UnaryCallInternal(context, method, request, response,
                      std::move(on_completion));
  }

  /// Setup a unary call to a named method \a method using
  /// \a context and specifying the \a request and \a response buffers.
  /// Like any other reactor-based RPC, it will not be activated until
  /// StartCall is invoked on its reactor.
  void PrepareUnaryCall(ClientContext* context, const std::string& method,
                        const RequestType* request, ResponseType* response,
                        ::grpc_impl::ClientUnaryReactor* reactor) {
    PrepareUnaryCallInternal(context, method, request, response, reactor);
  }

  /// Setup a call to a named method \a method using \a context and tied to
  /// \a reactor . Like any other bidi streaming RPC, it will not be activated
  /// until StartCall is invoked on its reactor.
  void PrepareBidiStreamingCall(
      ClientContext* context, const std::string& method,
      ::grpc_impl::ClientBidiReactor<RequestType, ResponseType>* reactor) {
    PrepareBidiStreamingCallInternal(context, method, reactor);
  }
#endif

  /// NOTE: class experimental_type is not part of the public API of this class
  /// TODO(vjpai): Move these contents to the public API of GenericStub when
  ///              they are no longer experimental
  class experimental_type {
   public:
    explicit experimental_type(TemplatedGenericStub* stub) : stub_(stub) {}

    /// Setup and start a unary call to a named method \a method using
    /// \a context and specifying the \a request and \a response buffers.
    void UnaryCall(ClientContext* context, const std::string& method,
                   const RequestType* request, ResponseType* response,
                   std::function<void(grpc::Status)> on_completion) {
      stub_->UnaryCallInternal(context, method, request, response,
                               std::move(on_completion));
    }

    /// Setup a unary call to a named method \a method using
    /// \a context and specifying the \a request and \a response buffers.
    /// Like any other reactor-based RPC, it will not be activated until
    /// StartCall is invoked on its reactor.
    void PrepareUnaryCall(ClientContext* context, const std::string& method,
                          const RequestType* request, ResponseType* response,
                          ::grpc_impl::ClientUnaryReactor* reactor) {
      stub_->PrepareUnaryCallInternal(context, method, request, response,
                                      reactor);
    }

    /// Setup a call to a named method \a method using \a context and tied to
    /// \a reactor . Like any other bidi streaming RPC, it will not be activated
    /// until StartCall is invoked on its reactor.
    void PrepareBidiStreamingCall(
        ClientContext* context, const std::string& method,
        ::grpc_impl::ClientBidiReactor<RequestType, ResponseType>* reactor) {
      stub_->PrepareBidiStreamingCallInternal(context, method, reactor);
    }

   private:
    TemplatedGenericStub* stub_;
  };

  /// NOTE: The function experimental() is not stable public API. It is a view
  /// to the experimental components of this class. It may be changed or removed
  /// at any time.
  experimental_type experimental() { return experimental_type(this); }

 private:
  std::shared_ptr<grpc::ChannelInterface> channel_;

  void UnaryCallInternal(ClientContext* context, const std::string& method,
                         const RequestType* request, ResponseType* response,
                         std::function<void(grpc::Status)> on_completion) {
    ::grpc_impl::internal::CallbackUnaryCall(
        channel_.get(),
        grpc::internal::RpcMethod(method.c_str(),
                                  grpc::internal::RpcMethod::NORMAL_RPC),
        context, request, response, std::move(on_completion));
  }

  void PrepareUnaryCallInternal(ClientContext* context,
                                const std::string& method,
                                const RequestType* request,
                                ResponseType* response,
                                ::grpc_impl::ClientUnaryReactor* reactor) {
    ::grpc_impl::internal::ClientCallbackUnaryFactory::Create<RequestType,
                                                              ResponseType>(
        channel_.get(),
        grpc::internal::RpcMethod(method.c_str(),
                                  grpc::internal::RpcMethod::NORMAL_RPC),
        context, request, response, reactor);
  }

  void PrepareBidiStreamingCallInternal(
      ClientContext* context, const std::string& method,
      ::grpc_impl::ClientBidiReactor<RequestType, ResponseType>* reactor) {
    ::grpc_impl::internal::
        ClientCallbackReaderWriterFactory<RequestType, ResponseType>::Create(
            channel_.get(),
            grpc::internal::RpcMethod(
                method.c_str(), grpc::internal::RpcMethod::BIDI_STREAMING),
            context, reactor);
  }

  std::unique_ptr<
      ::grpc_impl::ClientAsyncReaderWriter<RequestType, ResponseType>>
  CallInternal(grpc::ChannelInterface* channel, ClientContext* context,
               const std::string& method, ::grpc_impl::CompletionQueue* cq,
               bool start, void* tag) {
    return std::unique_ptr<
        ::grpc_impl::ClientAsyncReaderWriter<RequestType, ResponseType>>(
        ::grpc_impl::internal::
            ClientAsyncReaderWriterFactory<RequestType, ResponseType>::Create(
                channel, cq,
                grpc::internal::RpcMethod(
                    method.c_str(), grpc::internal::RpcMethod::BIDI_STREAMING),
                context, start, tag));
  }
};

typedef TemplatedGenericStub<grpc::ByteBuffer, grpc::ByteBuffer> GenericStub;

}  // namespace grpc

#endif  // GRPCPP_GENERIC_GENERIC_STUB_H
