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
#include <grpcpp/impl/codegen/call_op_set.h>
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
    ops->SendInitialMetadata(&context->send_initial_metadata_,
                             context->initial_metadata_flags());
    ops->RecvInitialMetadata(context);
    ops->RecvMessage(result);
    ops->AllowNoMessage();
    ops->ClientSendClose();
    ops->ClientRecvStatus(context, tag->status_ptr());
    ops->set_core_cq_tag(tag);
    call.PerformOps(ops);
  }
};
}  // namespace internal

namespace experimental {

// The user must implement this reactor interface with reactions to each event
// type that gets called by the library. An empty reaction is provided by
// default

class ClientBidiReactor {
 public:
  virtual ~ClientBidiReactor() {}
  virtual void OnDone(Status s) {}
  virtual void OnReadInitialMetadataDone(bool ok) {}
  virtual void OnReadDone(bool ok) {}
  virtual void OnWriteDone(bool ok) {}
  virtual void OnWritesDoneDone(bool ok) {}
};

class ClientReadReactor {
 public:
  virtual ~ClientReadReactor() {}
  virtual void OnDone(Status s) {}
  virtual void OnReadInitialMetadataDone(bool ok) {}
  virtual void OnReadDone(bool ok) {}
};

class ClientWriteReactor {
 public:
  virtual ~ClientWriteReactor() {}
  virtual void OnDone(Status s) {}
  virtual void OnReadInitialMetadataDone(bool ok) {}
  virtual void OnWriteDone(bool ok) {}
  virtual void OnWritesDoneDone(bool ok) {}
};

template <class Request, class Response>
class ClientCallbackReaderWriter {
 public:
  virtual ~ClientCallbackReaderWriter() {}
  virtual void StartCall() = 0;
  void Write(const Request* req) { Write(req, WriteOptions()); }
  virtual void Write(const Request* req, WriteOptions options) = 0;
  void WriteLast(const Request* req, WriteOptions options) {
    Write(req, options.set_last_message());
  }
  virtual void WritesDone() = 0;
  virtual void Read(Response* resp) = 0;
};

template <class Response>
class ClientCallbackReader {
 public:
  virtual ~ClientCallbackReader() {}
  virtual void StartCall() = 0;
  virtual void Read(Response* resp) = 0;
};

template <class Request>
class ClientCallbackWriter {
 public:
  virtual ~ClientCallbackWriter() {}
  virtual void StartCall() = 0;
  void Write(const Request* req) { Write(req, WriteOptions()); }
  virtual void Write(const Request* req, WriteOptions options) = 0;
  void WriteLast(const Request* req, WriteOptions options) {
    Write(req, options.set_last_message());
  }
  virtual void WritesDone() = 0;
};

}  // namespace experimental

namespace internal {

// Forward declare factory classes for friendship
template <class Request, class Response>
class ClientCallbackReaderWriterFactory;
template <class Response>
class ClientCallbackReaderFactory;
template <class Request>
class ClientCallbackWriterFactory;

template <class Request, class Response>
class ClientCallbackReaderWriterImpl
    : public ::grpc::experimental::ClientCallbackReaderWriter<Request,
                                                              Response> {
 public:
  // always allocated against a call arena, no memory free required
  static void operator delete(void* ptr, std::size_t size) {
    assert(size == sizeof(ClientCallbackReaderWriterImpl));
  }

  // This operator should never be called as the memory should be freed as part
  // of the arena destruction. It only exists to provide a matching operator
  // delete to the operator new so that some compilers will not complain (see
  // https://github.com/grpc/grpc/issues/11301) Note at the time of adding this
  // there are no tests catching the compiler warning.
  static void operator delete(void*, void*) { assert(0); }

  void MaybeFinish() {
    if (--callbacks_outstanding_ == 0) {
      reactor_->OnDone(std::move(finish_status_));
      auto* call = call_.call();
      this->~ClientCallbackReaderWriterImpl();
      g_core_codegen_interface->grpc_call_unref(call);
    }
  }

  void StartCall() override {
    // This call initiates two batches
    // 1. Send initial metadata (unless corked)/recv initial metadata
    // 2. Recv trailing metadata, on_completion callback
    callbacks_outstanding_ = 2;

    start_tag_.Set(call_.call(),
                   [this](bool ok) {
                     reactor_->OnReadInitialMetadataDone(ok);
                     MaybeFinish();
                   },
                   &start_ops_);
    start_corked_ = context_->initial_metadata_corked_;
    if (!start_corked_) {
      start_ops_.SendInitialMetadata(&context_->send_initial_metadata_,
                                     context_->initial_metadata_flags());
    }
    start_ops_.RecvInitialMetadata(context_);
    start_ops_.set_core_cq_tag(&start_tag_);
    call_.PerformOps(&start_ops_);

    finish_tag_.Set(call_.call(), [this](bool ok) { MaybeFinish(); },
                    &finish_ops_);
    finish_ops_.ClientRecvStatus(context_, &finish_status_);
    finish_ops_.set_core_cq_tag(&finish_tag_);
    call_.PerformOps(&finish_ops_);

    // Also set up the read and write tags so that they don't have to be set up
    // each time
    write_tag_.Set(call_.call(),
                   [this](bool ok) {
                     reactor_->OnWriteDone(ok);
                     MaybeFinish();
                   },
                   &write_ops_);
    write_ops_.set_core_cq_tag(&write_tag_);

    read_tag_.Set(call_.call(),
                  [this](bool ok) {
                    reactor_->OnReadDone(ok);
                    MaybeFinish();
                  },
                  &read_ops_);
    read_ops_.set_core_cq_tag(&read_tag_);
  }

  void Read(Response* msg) override {
    read_ops_.RecvMessage(msg);
    callbacks_outstanding_++;
    call_.PerformOps(&read_ops_);
  }

  void Write(const Request* msg, WriteOptions options) override {
    if (start_corked_) {
      write_ops_.SendInitialMetadata(&context_->send_initial_metadata_,
                                     context_->initial_metadata_flags());
      start_corked_ = false;
    }
    // TODO(vjpai): don't assert
    GPR_CODEGEN_ASSERT(write_ops_.SendMessage(*msg).ok());

    if (options.is_last_message()) {
      options.set_buffer_hint();
      write_ops_.ClientSendClose();
    }
    callbacks_outstanding_++;
    call_.PerformOps(&write_ops_);
  }
  void WritesDone() override {
    if (start_corked_) {
      writes_done_ops_.SendInitialMetadata(&context_->send_initial_metadata_,
                                           context_->initial_metadata_flags());
      start_corked_ = false;
    }
    writes_done_ops_.ClientSendClose();
    writes_done_tag_.Set(call_.call(),
                         [this](bool ok) {
                           reactor_->OnWritesDoneDone(ok);
                           MaybeFinish();
                         },
                         &writes_done_ops_);
    writes_done_ops_.set_core_cq_tag(&writes_done_tag_);
    callbacks_outstanding_++;
    call_.PerformOps(&writes_done_ops_);
  }

 private:
  friend class ClientCallbackReaderWriterFactory<Request, Response>;

  ClientCallbackReaderWriterImpl(
      Call call, ClientContext* context,
      ::grpc::experimental::ClientBidiReactor* reactor)
      : context_(context), call_(call), reactor_(reactor) {}

  ClientContext* context_;
  Call call_;
  ::grpc::experimental::ClientBidiReactor* reactor_;

  CallOpSet<CallOpSendInitialMetadata, CallOpRecvInitialMetadata> start_ops_;
  CallbackWithSuccessTag start_tag_;
  bool start_corked_;

  CallOpSet<CallOpClientRecvStatus> finish_ops_;
  CallbackWithSuccessTag finish_tag_;
  Status finish_status_;

  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage, CallOpClientSendClose>
      write_ops_;
  CallbackWithSuccessTag write_tag_;

  CallOpSet<CallOpSendInitialMetadata, CallOpClientSendClose> writes_done_ops_;
  CallbackWithSuccessTag writes_done_tag_;

  CallOpSet<CallOpRecvInitialMetadata, CallOpRecvMessage<Response>> read_ops_;
  CallbackWithSuccessTag read_tag_;

  std::atomic_int callbacks_outstanding_;
};

template <class Request, class Response>
class ClientCallbackReaderWriterFactory {
 public:
  static experimental::ClientCallbackReaderWriter<Request, Response>* Create(
      ChannelInterface* channel, const ::grpc::internal::RpcMethod& method,
      ClientContext* context,
      ::grpc::experimental::ClientBidiReactor* reactor) {
    Call call = channel->CreateCall(method, context, channel->CallbackCQ());

    g_core_codegen_interface->grpc_call_ref(call.call());
    return new (g_core_codegen_interface->grpc_call_arena_alloc(
        call.call(), sizeof(ClientCallbackReaderWriterImpl<Request, Response>)))
        ClientCallbackReaderWriterImpl<Request, Response>(call, context,
                                                          reactor);
  }
};

template <class Response>
class ClientCallbackReaderImpl
    : public ::grpc::experimental::ClientCallbackReader<Response> {
 public:
  // always allocated against a call arena, no memory free required
  static void operator delete(void* ptr, std::size_t size) {
    assert(size == sizeof(ClientCallbackReaderImpl));
  }

  // This operator should never be called as the memory should be freed as part
  // of the arena destruction. It only exists to provide a matching operator
  // delete to the operator new so that some compilers will not complain (see
  // https://github.com/grpc/grpc/issues/11301) Note at the time of adding this
  // there are no tests catching the compiler warning.
  static void operator delete(void*, void*) { assert(0); }

  void MaybeFinish() {
    if (--callbacks_outstanding_ == 0) {
      reactor_->OnDone(std::move(finish_status_));
      auto* call = call_.call();
      this->~ClientCallbackReaderImpl();
      g_core_codegen_interface->grpc_call_unref(call);
    }
  }

  void StartCall() override {
    // This call initiates two batches
    // 1. Send initial metadata (unless corked)/recv initial metadata
    // 2. Recv trailing metadata, on_completion callback
    callbacks_outstanding_ = 2;

    start_tag_.Set(call_.call(),
                   [this](bool ok) {
                     reactor_->OnReadInitialMetadataDone(ok);
                     MaybeFinish();
                   },
                   &start_ops_);
    start_ops_.SendInitialMetadata(&context_->send_initial_metadata_,
                                   context_->initial_metadata_flags());
    start_ops_.RecvInitialMetadata(context_);
    start_ops_.set_core_cq_tag(&start_tag_);
    call_.PerformOps(&start_ops_);

    finish_tag_.Set(call_.call(), [this](bool ok) { MaybeFinish(); },
                    &finish_ops_);
    finish_ops_.ClientRecvStatus(context_, &finish_status_);
    finish_ops_.set_core_cq_tag(&finish_tag_);
    call_.PerformOps(&finish_ops_);

    // Also set up the read tag so it doesn't have to be set up each time
    read_tag_.Set(call_.call(),
                  [this](bool ok) {
                    reactor_->OnReadDone(ok);
                    MaybeFinish();
                  },
                  &read_ops_);
    read_ops_.set_core_cq_tag(&read_tag_);
  }

  void Read(Response* msg) override {
    read_ops_.RecvMessage(msg);
    callbacks_outstanding_++;
    call_.PerformOps(&read_ops_);
  }

 private:
  friend class ClientCallbackReaderFactory<Response>;

  template <class Request>
  ClientCallbackReaderImpl(Call call, ClientContext* context, Request* request,
                           ::grpc::experimental::ClientReadReactor* reactor)
      : context_(context), call_(call), reactor_(reactor) {
    // TODO(vjpai): don't assert
    GPR_CODEGEN_ASSERT(start_ops_.SendMessage(*request).ok());
    start_ops_.ClientSendClose();
  }

  ClientContext* context_;
  Call call_;
  ::grpc::experimental::ClientReadReactor* reactor_;

  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage, CallOpClientSendClose,
            CallOpRecvInitialMetadata>
      start_ops_;
  CallbackWithSuccessTag start_tag_;

  CallOpSet<CallOpClientRecvStatus> finish_ops_;
  CallbackWithSuccessTag finish_tag_;
  Status finish_status_;

  CallOpSet<CallOpRecvInitialMetadata, CallOpRecvMessage<Response>> read_ops_;
  CallbackWithSuccessTag read_tag_;

  std::atomic_int callbacks_outstanding_;
};

template <class Response>
class ClientCallbackReaderFactory {
 public:
  template <class Request>
  static experimental::ClientCallbackReader<Response>* Create(
      ChannelInterface* channel, const ::grpc::internal::RpcMethod& method,
      ClientContext* context, const Request* request,
      ::grpc::experimental::ClientReadReactor* reactor) {
    Call call = channel->CreateCall(method, context, channel->CallbackCQ());

    g_core_codegen_interface->grpc_call_ref(call.call());
    return new (g_core_codegen_interface->grpc_call_arena_alloc(
        call.call(), sizeof(ClientCallbackReaderImpl<Response>)))
        ClientCallbackReaderImpl<Response>(call, context, request, reactor);
  }
};

template <class Request>
class ClientCallbackWriterImpl
    : public ::grpc::experimental::ClientCallbackWriter<Request> {
 public:
  // always allocated against a call arena, no memory free required
  static void operator delete(void* ptr, std::size_t size) {
    assert(size == sizeof(ClientCallbackWriterImpl));
  }

  // This operator should never be called as the memory should be freed as part
  // of the arena destruction. It only exists to provide a matching operator
  // delete to the operator new so that some compilers will not complain (see
  // https://github.com/grpc/grpc/issues/11301) Note at the time of adding this
  // there are no tests catching the compiler warning.
  static void operator delete(void*, void*) { assert(0); }

  void MaybeFinish() {
    if (--callbacks_outstanding_ == 0) {
      reactor_->OnDone(std::move(finish_status_));
      auto* call = call_.call();
      this->~ClientCallbackWriterImpl();
      g_core_codegen_interface->grpc_call_unref(call);
    }
  }

  void StartCall() override {
    // This call initiates two batches
    // 1. Send initial metadata (unless corked)/recv initial metadata
    // 2. Recv message + trailing metadata, on_completion callback
    callbacks_outstanding_ = 2;

    start_tag_.Set(call_.call(),
                   [this](bool ok) {
                     reactor_->OnReadInitialMetadataDone(ok);
                     MaybeFinish();
                   },
                   &start_ops_);
    start_corked_ = context_->initial_metadata_corked_;
    if (!start_corked_) {
      start_ops_.SendInitialMetadata(&context_->send_initial_metadata_,
                                     context_->initial_metadata_flags());
    }
    start_ops_.RecvInitialMetadata(context_);
    start_ops_.set_core_cq_tag(&start_tag_);
    call_.PerformOps(&start_ops_);

    finish_tag_.Set(call_.call(), [this](bool ok) { MaybeFinish(); },
                    &finish_ops_);
    finish_ops_.ClientRecvStatus(context_, &finish_status_);
    finish_ops_.set_core_cq_tag(&finish_tag_);
    call_.PerformOps(&finish_ops_);

    // Also set up the read and write tags so that they don't have to be set up
    // each time
    write_tag_.Set(call_.call(),
                   [this](bool ok) {
                     reactor_->OnWriteDone(ok);
                     MaybeFinish();
                   },
                   &write_ops_);
    write_ops_.set_core_cq_tag(&write_tag_);
  }

  void Write(const Request* msg, WriteOptions options) override {
    if (start_corked_) {
      write_ops_.SendInitialMetadata(&context_->send_initial_metadata_,
                                     context_->initial_metadata_flags());
      start_corked_ = false;
    }
    // TODO(vjpai): don't assert
    GPR_CODEGEN_ASSERT(write_ops_.SendMessage(*msg).ok());

    if (options.is_last_message()) {
      options.set_buffer_hint();
      write_ops_.ClientSendClose();
    }
    callbacks_outstanding_++;
    call_.PerformOps(&write_ops_);
  }
  void WritesDone() override {
    if (start_corked_) {
      writes_done_ops_.SendInitialMetadata(&context_->send_initial_metadata_,
                                           context_->initial_metadata_flags());
      start_corked_ = false;
    }
    writes_done_ops_.ClientSendClose();
    writes_done_tag_.Set(call_.call(),
                         [this](bool ok) {
                           reactor_->OnWritesDoneDone(ok);
                           MaybeFinish();
                         },
                         &writes_done_ops_);
    writes_done_ops_.set_core_cq_tag(&writes_done_tag_);
    callbacks_outstanding_++;
    call_.PerformOps(&writes_done_ops_);
  }

 private:
  friend class ClientCallbackWriterFactory<Request>;

  template <class Response>
  ClientCallbackWriterImpl(Call call, ClientContext* context,
                           Response* response,
                           ::grpc::experimental::ClientWriteReactor* reactor)
      : context_(context), call_(call), reactor_(reactor) {
    finish_ops_.RecvMessage(response);
    finish_ops_.AllowNoMessage();
  }

  ClientContext* context_;
  Call call_;
  ::grpc::experimental::ClientWriteReactor* reactor_;

  CallOpSet<CallOpSendInitialMetadata, CallOpRecvInitialMetadata> start_ops_;
  CallbackWithSuccessTag start_tag_;
  bool start_corked_;

  CallOpSet<CallOpGenericRecvMessage, CallOpClientRecvStatus> finish_ops_;
  CallbackWithSuccessTag finish_tag_;
  Status finish_status_;

  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage, CallOpClientSendClose>
      write_ops_;
  CallbackWithSuccessTag write_tag_;

  CallOpSet<CallOpSendInitialMetadata, CallOpClientSendClose> writes_done_ops_;
  CallbackWithSuccessTag writes_done_tag_;

  std::atomic_int callbacks_outstanding_;
};

template <class Request>
class ClientCallbackWriterFactory {
 public:
  template <class Response>
  static experimental::ClientCallbackWriter<Request>* Create(
      ChannelInterface* channel, const ::grpc::internal::RpcMethod& method,
      ClientContext* context, Response* response,
      ::grpc::experimental::ClientWriteReactor* reactor) {
    Call call = channel->CreateCall(method, context, channel->CallbackCQ());

    g_core_codegen_interface->grpc_call_ref(call.call());
    return new (g_core_codegen_interface->grpc_call_arena_alloc(
        call.call(), sizeof(ClientCallbackWriterImpl<Request>)))
        ClientCallbackWriterImpl<Request>(call, context, response, reactor);
  }
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CLIENT_CALLBACK_H
