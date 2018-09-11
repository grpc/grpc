/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_CLIENT_CALLBACK_H
#define GRPCPP_IMPL_CODEGEN_CLIENT_CALLBACK_H

#include <functional>

#include <grpcpp/impl/codegen/call.h>
#include <grpcpp/impl/codegen/callback_common.h>
#include <grpcpp/impl/codegen/channel_interface.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/status.h>

namespace grpc {

class Channel;
class ClientContext;
class CompletionQueue;

namespace internal {
class RpcMethod;

/// Perform a callback-based unary call
/// TODO(vjpai): Combine as much as possible with the blocking unary call code
template <class InputMessage, class OutputMessage>
void CallbackUnaryCall(ChannelInterface* channel, const RpcMethod& method,
                       ClientContext* context, const InputMessage* request,
                       OutputMessage* result,
                       std::function<void(Status)> on_completion) {
  CallbackUnaryCallImpl<InputMessage, OutputMessage> x(
      channel, method, context, request, result, on_completion);
}

template <class InputMessage, class OutputMessage>
class CallbackUnaryCallImpl {
 public:
  CallbackUnaryCallImpl(ChannelInterface* channel, const RpcMethod& method,
                        ClientContext* context, const InputMessage* request,
                        OutputMessage* result,
                        std::function<void(Status)> on_completion) {
    CompletionQueue* cq = channel->CallbackCQ();
    GPR_CODEGEN_ASSERT(cq != nullptr);
    Call call(channel->CreateCall(method, context, cq));

    using FullCallOpSet =
        CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
                  CallOpRecvInitialMetadata, CallOpRecvMessage<OutputMessage>,
                  CallOpClientSendClose, CallOpClientRecvStatus>;

    auto* ops = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call.call(), sizeof(FullCallOpSet))) FullCallOpSet;

    auto* tag = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call.call(), sizeof(CallbackWithStatusTag)))
        CallbackWithStatusTag(call.call(), on_completion, ops);

    // TODO(vjpai): Unify code with sync API as much as possible
    Status s = ops->SendMessage(*request);
    if (!s.ok()) {
      tag->force_run(s);
      return;
    }
    ops->SendInitialMetadata(context->send_initial_metadata_,
                             context->initial_metadata_flags());
    ops->RecvInitialMetadata(context);
    ops->RecvMessage(result);
    ops->AllowNoMessage();
    ops->ClientSendClose();
    ops->ClientRecvStatus(context, tag->status_ptr());
    ops->set_cq_tag(tag->tag());
    call.PerformOps(ops);
  }
};

}  // namespace internal

/// TODO(vjpai): Move these contents out of experimental
/// TODO(vjpai): Merge with contents of sync_stream.h or async_stream.h if
/// possible, including getting their interfaces into the interface classes
namespace experimental {

// Forward declaration
template <class W, class R>
class ClientCallbackReaderWriter;

/// TODO(vjpai): Put this factory into ::grpc::internal when possible
template <class W, class R>
class ClientCallbackReaderWriterFactory {
 public:
  static ::grpc::experimental::ClientCallbackReaderWriter<W, R>* Create(
      ::grpc::ChannelInterface* channel,
      const ::grpc::internal::RpcMethod& method, ClientContext* context) {
    ::grpc::internal::Call call =
        channel->CreateCall(method, context, channel->CallbackCQ());
    return new (g_core_codegen_interface->grpc_call_arena_alloc(
        call.call(),
        sizeof(::grpc::experimental::ClientCallbackReaderWriter<W, R>)))::grpc::
        experimental::ClientCallbackReaderWriter<W, R>(call, context);
  }
};

/// Callback-based client-side API for bi-directional streaming RPCs,
/// where the outgoing message stream coming from the client has messages of
/// type \a W, and the incoming messages stream coming from the server has
/// messages of type \a R.
/// TODO(vjpai): make this a derived class of an interface class
template <class W, class R>
class ClientCallbackReaderWriter final {
 public:
  // always allocated against a call arena, no memory free required
  static void operator delete(void* ptr, std::size_t size) {
    assert(size == sizeof(ClientCallbackReaderWriter));
  }

  // This operator should never be called as the memory should be freed as part
  // of the arena destruction. It only exists to provide a matching operator
  // delete to the operator new so that some compilers will not complain (see
  // https://github.com/grpc/grpc/issues/11301) Note at the time of adding this
  // there are no tests catching the compiler warning.
  static void operator delete(void*, void*) { assert(0); }

  void StartCall(std::function<void(Status)> on_completion) {
    StartCall(std::move(on_completion), nullptr);
  }

  void StartCall(std::function<void(Status)> on_completion,
                 std::function<void(bool)> on_metadata_available) {
    // This call may initiate two batches
    // 1. Send initial metadata/recv initial metadata, optional callback
    // 2. Recv trailing metadata, on_completion callback

    auto* tag1 = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call_.call(), sizeof(::grpc::internal::CallbackWithSuccessTag)))::grpc::
        internal::CallbackWithSuccessTag(
            call_.call(),
            on_metadata_available ? on_metadata_available : [](bool) {},
            &start_ops_);

    if (!context_->initial_metadata_corked_) {
      start_ops_.SendInitialMetadata(context_->send_initial_metadata_,
                                     context_->initial_metadata_flags());
    }
    start_ops_.RecvInitialMetadata(context_);
    start_ops_.set_cq_tag(tag1->tag());
    call_.PerformOps(&start_ops_);

    auto* tag2 = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call_.call(), sizeof(::grpc::internal::CallbackWithStatusTag)))::grpc::
        internal::CallbackWithStatusTag(call_.call(), on_completion,
                                        &finish_ops_);

    finish_ops_.ClientRecvStatus(context_, tag2->status_ptr());
    finish_ops_.set_cq_tag(tag2->tag());
    call_.PerformOps(&finish_ops_);
  }

  void Read(R* msg, std::function<void(bool)> f) {
    auto* tag = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call_.call(), sizeof(::grpc::internal::CallbackWithSuccessTag)))::grpc::
        internal::CallbackWithSuccessTag(call_.call(), f, &read_ops_);
    read_ops_.RecvMessage(msg);
    read_ops_.set_cq_tag(tag->tag());
    call_.PerformOps(&read_ops_);
  }

  void Write(const W* msg, std::function<void(bool)> f) {
    Write(msg, WriteOptions(), std::move(f));
  }

  void Write(const W* msg, WriteOptions options, std::function<void(bool)> f) {
    auto* tag = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call_.call(), sizeof(::grpc::internal::CallbackWithSuccessTag)))::grpc::
        internal::CallbackWithSuccessTag(call_.call(), f, &write_ops_);
    if (context_->initial_metadata_corked_) {
      write_ops_.SendInitialMetadata(context_->send_initial_metadata_,
                                     context_->initial_metadata_flags());
    }
    // TODO(vjpai): don't assert
    GPR_CODEGEN_ASSERT(write_ops_.SendMessage(*msg).ok());
    if (options.is_last_message()) {
      options.set_buffer_hint();
      write_ops_.ClientSendClose();
    }
    write_ops_.set_cq_tag(tag->tag());
    call_.PerformOps(&write_ops_);
  }

  void WritesDone(std::function<void(bool)> f) {
    auto* tag = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call_.call(), sizeof(::grpc::internal::CallbackWithSuccessTag)))::grpc::
        internal::CallbackWithSuccessTag(call_.call(), f, &write_ops_);
    write_ops_.set_cq_tag(tag->tag());
    write_ops_.ClientSendClose();
    call_.PerformOps(&write_ops_);
  }

 private:
  friend class experimental::ClientCallbackReaderWriterFactory<W, R>;
  ClientCallbackReaderWriter(::grpc::internal::Call call,
                             ClientContext* context)
      : context_(context), call_(call) {}

  ClientContext* context_;
  ::grpc::internal::Call call_;
  ::grpc::internal::CallOpSet<::grpc::internal::CallOpSendInitialMetadata,
                              ::grpc::internal::CallOpRecvInitialMetadata>
      start_ops_;
  ::grpc::internal::CallOpSet<::grpc::internal::CallOpRecvMessage<R>> read_ops_;
  ::grpc::internal::CallOpSet<::grpc::internal::CallOpSendInitialMetadata,
                              ::grpc::internal::CallOpSendMessage,
                              ::grpc::internal::CallOpClientSendClose>
      write_ops_;
  ::grpc::internal::CallOpSet<::grpc::internal::CallOpClientRecvStatus>
      finish_ops_;
};

}  // namespace experimental

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CLIENT_CALLBACK_H
