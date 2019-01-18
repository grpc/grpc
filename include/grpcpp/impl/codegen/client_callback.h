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
    Status s = ops->SendMessagePtr(request);
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

// Forward declarations
template <class Request, class Response>
class ClientBidiReactor;
template <class Response>
class ClientReadReactor;
template <class Request>
class ClientWriteReactor;

// NOTE: The streaming objects are not actually implemented in the public API.
//       These interfaces are provided for mocking only. Typical applications
//       will interact exclusively with the reactors that they define.
template <class Request, class Response>
class ClientCallbackReaderWriter {
 public:
  virtual ~ClientCallbackReaderWriter() {}
  virtual void StartCall() = 0;
  virtual void Write(const Request* req, WriteOptions options) = 0;
  virtual void WritesDone() = 0;
  virtual void Read(Response* resp) = 0;

 protected:
  void BindReactor(ClientBidiReactor<Request, Response>* reactor) {
    reactor->BindStream(this);
  }
};

template <class Response>
class ClientCallbackReader {
 public:
  virtual ~ClientCallbackReader() {}
  virtual void StartCall() = 0;
  virtual void Read(Response* resp) = 0;

 protected:
  void BindReactor(ClientReadReactor<Response>* reactor) {
    reactor->BindReader(this);
  }
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

 protected:
  void BindReactor(ClientWriteReactor<Request>* reactor) {
    reactor->BindWriter(this);
  }
};

// The user must implement this reactor interface with reactions to each event
// type that gets called by the library. An empty reaction is provided by
// default
template <class Request, class Response>
class ClientBidiReactor {
 public:
  virtual ~ClientBidiReactor() {}
  virtual void OnDone(const Status& s) {}
  virtual void OnReadInitialMetadataDone(bool ok) {}
  virtual void OnReadDone(bool ok) {}
  virtual void OnWriteDone(bool ok) {}
  virtual void OnWritesDoneDone(bool ok) {}

  void StartCall() { stream_->StartCall(); }
  void StartRead(Response* resp) { stream_->Read(resp); }
  void StartWrite(const Request* req) { StartWrite(req, WriteOptions()); }
  void StartWrite(const Request* req, WriteOptions options) {
    stream_->Write(req, std::move(options));
  }
  void StartWriteLast(const Request* req, WriteOptions options) {
    StartWrite(req, std::move(options.set_last_message()));
  }
  void StartWritesDone() { stream_->WritesDone(); }

 private:
  friend class ClientCallbackReaderWriter<Request, Response>;
  void BindStream(ClientCallbackReaderWriter<Request, Response>* stream) {
    stream_ = stream;
  }
  ClientCallbackReaderWriter<Request, Response>* stream_;
};

template <class Response>
class ClientReadReactor {
 public:
  virtual ~ClientReadReactor() {}
  virtual void OnDone(const Status& s) {}
  virtual void OnReadInitialMetadataDone(bool ok) {}
  virtual void OnReadDone(bool ok) {}

  void StartCall() { reader_->StartCall(); }
  void StartRead(Response* resp) { reader_->Read(resp); }

 private:
  friend class ClientCallbackReader<Response>;
  void BindReader(ClientCallbackReader<Response>* reader) { reader_ = reader; }
  ClientCallbackReader<Response>* reader_;
};

template <class Request>
class ClientWriteReactor {
 public:
  virtual ~ClientWriteReactor() {}
  virtual void OnDone(const Status& s) {}
  virtual void OnReadInitialMetadataDone(bool ok) {}
  virtual void OnWriteDone(bool ok) {}
  virtual void OnWritesDoneDone(bool ok) {}

  void StartCall() { writer_->StartCall(); }
  void StartWrite(const Request* req) { StartWrite(req, WriteOptions()); }
  void StartWrite(const Request* req, WriteOptions options) {
    writer_->Write(req, std::move(options));
  }
  void StartWriteLast(const Request* req, WriteOptions options) {
    StartWrite(req, std::move(options.set_last_message()));
  }
  void StartWritesDone() { writer_->WritesDone(); }

 private:
  friend class ClientCallbackWriter<Request>;
  void BindWriter(ClientCallbackWriter<Request>* writer) { writer_ = writer; }
  ClientCallbackWriter<Request>* writer_;
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
      Status s = std::move(finish_status_);
      auto* reactor = reactor_;
      auto* call = call_.call();
      this->~ClientCallbackReaderWriterImpl();
      g_core_codegen_interface->grpc_call_unref(call);
      reactor->OnDone(s);
    }
  }

  void StartCall() override {
    // This call initiates two batches, plus any backlog, each with a callback
    // 1. Send initial metadata (unless corked) + recv initial metadata
    // 2. Any read backlog
    // 3. Recv trailing metadata, on_completion callback
    // 4. Any write backlog
    // 5. See if the call can finish (if other callbacks were triggered already)
    started_ = true;

    start_tag_.Set(call_.call(),
                   [this](bool ok) {
                     reactor_->OnReadInitialMetadataDone(ok);
                     MaybeFinish();
                   },
                   &start_ops_);
    if (!start_corked_) {
      start_ops_.SendInitialMetadata(&context_->send_initial_metadata_,
                                     context_->initial_metadata_flags());
    }
    start_ops_.RecvInitialMetadata(context_);
    start_ops_.set_core_cq_tag(&start_tag_);
    call_.PerformOps(&start_ops_);

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
    if (read_ops_at_start_) {
      call_.PerformOps(&read_ops_);
    }

    finish_tag_.Set(call_.call(), [this](bool ok) { MaybeFinish(); },
                    &finish_ops_);
    finish_ops_.ClientRecvStatus(context_, &finish_status_);
    finish_ops_.set_core_cq_tag(&finish_tag_);
    call_.PerformOps(&finish_ops_);

    if (write_ops_at_start_) {
      call_.PerformOps(&write_ops_);
    }

    if (writes_done_ops_at_start_) {
      call_.PerformOps(&writes_done_ops_);
    }
    MaybeFinish();
  }

  void Read(Response* msg) override {
    read_ops_.RecvMessage(msg);
    callbacks_outstanding_++;
    if (started_) {
      call_.PerformOps(&read_ops_);
    } else {
      read_ops_at_start_ = true;
    }
  }

  void Write(const Request* msg, WriteOptions options) override {
    if (start_corked_) {
      write_ops_.SendInitialMetadata(&context_->send_initial_metadata_,
                                     context_->initial_metadata_flags());
      start_corked_ = false;
    }

    if (options.is_last_message()) {
      options.set_buffer_hint();
      write_ops_.ClientSendClose();
    }
    // TODO(vjpai): don't assert
    GPR_CODEGEN_ASSERT(write_ops_.SendMessagePtr(msg, options).ok());
    callbacks_outstanding_++;
    if (started_) {
      call_.PerformOps(&write_ops_);
    } else {
      write_ops_at_start_ = true;
    }
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
    if (started_) {
      call_.PerformOps(&writes_done_ops_);
    } else {
      writes_done_ops_at_start_ = true;
    }
  }

 private:
  friend class ClientCallbackReaderWriterFactory<Request, Response>;

  ClientCallbackReaderWriterImpl(
      Call call, ClientContext* context,
      ::grpc::experimental::ClientBidiReactor<Request, Response>* reactor)
      : context_(context),
        call_(call),
        reactor_(reactor),
        start_corked_(context_->initial_metadata_corked_) {
    this->BindReactor(reactor);
  }

  ClientContext* context_;
  Call call_;
  ::grpc::experimental::ClientBidiReactor<Request, Response>* reactor_;

  CallOpSet<CallOpSendInitialMetadata, CallOpRecvInitialMetadata> start_ops_;
  CallbackWithSuccessTag start_tag_;
  bool start_corked_;

  CallOpSet<CallOpClientRecvStatus> finish_ops_;
  CallbackWithSuccessTag finish_tag_;
  Status finish_status_;

  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage, CallOpClientSendClose>
      write_ops_;
  CallbackWithSuccessTag write_tag_;
  bool write_ops_at_start_{false};

  CallOpSet<CallOpSendInitialMetadata, CallOpClientSendClose> writes_done_ops_;
  CallbackWithSuccessTag writes_done_tag_;
  bool writes_done_ops_at_start_{false};

  CallOpSet<CallOpRecvMessage<Response>> read_ops_;
  CallbackWithSuccessTag read_tag_;
  bool read_ops_at_start_{false};

  // Minimum of 3 callbacks to pre-register for StartCall, start, and finish
  std::atomic_int callbacks_outstanding_{3};
  bool started_{false};
};

template <class Request, class Response>
class ClientCallbackReaderWriterFactory {
 public:
  static void Create(
      ChannelInterface* channel, const ::grpc::internal::RpcMethod& method,
      ClientContext* context,
      ::grpc::experimental::ClientBidiReactor<Request, Response>* reactor) {
    Call call = channel->CreateCall(method, context, channel->CallbackCQ());

    g_core_codegen_interface->grpc_call_ref(call.call());
    new (g_core_codegen_interface->grpc_call_arena_alloc(
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
      Status s = std::move(finish_status_);
      auto* reactor = reactor_;
      auto* call = call_.call();
      this->~ClientCallbackReaderImpl();
      g_core_codegen_interface->grpc_call_unref(call);
      reactor->OnDone(s);
    }
  }

  void StartCall() override {
    // This call initiates two batches, plus any backlog, each with a callback
    // 1. Send initial metadata (unless corked) + recv initial metadata
    // 2. Any backlog
    // 3. Recv trailing metadata, on_completion callback
    // 4. See if the call can finish (if other callbacks were triggered already)
    started_ = true;

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

    // Also set up the read tag so it doesn't have to be set up each time
    read_tag_.Set(call_.call(),
                  [this](bool ok) {
                    reactor_->OnReadDone(ok);
                    MaybeFinish();
                  },
                  &read_ops_);
    read_ops_.set_core_cq_tag(&read_tag_);
    if (read_ops_at_start_) {
      call_.PerformOps(&read_ops_);
    }

    finish_tag_.Set(call_.call(), [this](bool ok) { MaybeFinish(); },
                    &finish_ops_);
    finish_ops_.ClientRecvStatus(context_, &finish_status_);
    finish_ops_.set_core_cq_tag(&finish_tag_);
    call_.PerformOps(&finish_ops_);

    MaybeFinish();
  }

  void Read(Response* msg) override {
    read_ops_.RecvMessage(msg);
    callbacks_outstanding_++;
    if (started_) {
      call_.PerformOps(&read_ops_);
    } else {
      read_ops_at_start_ = true;
    }
  }

 private:
  friend class ClientCallbackReaderFactory<Response>;

  template <class Request>
  ClientCallbackReaderImpl(
      Call call, ClientContext* context, Request* request,
      ::grpc::experimental::ClientReadReactor<Response>* reactor)
      : context_(context), call_(call), reactor_(reactor) {
    this->BindReactor(reactor);
    // TODO(vjpai): don't assert
    GPR_CODEGEN_ASSERT(start_ops_.SendMessagePtr(request).ok());
    start_ops_.ClientSendClose();
  }

  ClientContext* context_;
  Call call_;
  ::grpc::experimental::ClientReadReactor<Response>* reactor_;

  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage, CallOpClientSendClose,
            CallOpRecvInitialMetadata>
      start_ops_;
  CallbackWithSuccessTag start_tag_;

  CallOpSet<CallOpClientRecvStatus> finish_ops_;
  CallbackWithSuccessTag finish_tag_;
  Status finish_status_;

  CallOpSet<CallOpRecvMessage<Response>> read_ops_;
  CallbackWithSuccessTag read_tag_;
  bool read_ops_at_start_{false};

  // Minimum of 3 callbacks to pre-register for StartCall, start, and finish
  std::atomic_int callbacks_outstanding_{3};
  bool started_{false};
};

template <class Response>
class ClientCallbackReaderFactory {
 public:
  template <class Request>
  static void Create(
      ChannelInterface* channel, const ::grpc::internal::RpcMethod& method,
      ClientContext* context, const Request* request,
      ::grpc::experimental::ClientReadReactor<Response>* reactor) {
    Call call = channel->CreateCall(method, context, channel->CallbackCQ());

    g_core_codegen_interface->grpc_call_ref(call.call());
    new (g_core_codegen_interface->grpc_call_arena_alloc(
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
      Status s = std::move(finish_status_);
      auto* reactor = reactor_;
      auto* call = call_.call();
      this->~ClientCallbackWriterImpl();
      g_core_codegen_interface->grpc_call_unref(call);
      reactor->OnDone(s);
    }
  }

  void StartCall() override {
    // This call initiates two batches, plus any backlog, each with a callback
    // 1. Send initial metadata (unless corked) + recv initial metadata
    // 2. Recv trailing metadata, on_completion callback
    // 3. Any backlog
    // 4. See if the call can finish (if other callbacks were triggered already)
    started_ = true;

    start_tag_.Set(call_.call(),
                   [this](bool ok) {
                     reactor_->OnReadInitialMetadataDone(ok);
                     MaybeFinish();
                   },
                   &start_ops_);
    if (!start_corked_) {
      start_ops_.SendInitialMetadata(&context_->send_initial_metadata_,
                                     context_->initial_metadata_flags());
    }
    start_ops_.RecvInitialMetadata(context_);
    start_ops_.set_core_cq_tag(&start_tag_);
    call_.PerformOps(&start_ops_);

    // Also set up the read and write tags so that they don't have to be set up
    // each time
    write_tag_.Set(call_.call(),
                   [this](bool ok) {
                     reactor_->OnWriteDone(ok);
                     MaybeFinish();
                   },
                   &write_ops_);
    write_ops_.set_core_cq_tag(&write_tag_);

    finish_tag_.Set(call_.call(), [this](bool ok) { MaybeFinish(); },
                    &finish_ops_);
    finish_ops_.ClientRecvStatus(context_, &finish_status_);
    finish_ops_.set_core_cq_tag(&finish_tag_);
    call_.PerformOps(&finish_ops_);

    if (write_ops_at_start_) {
      call_.PerformOps(&write_ops_);
    }

    if (writes_done_ops_at_start_) {
      call_.PerformOps(&writes_done_ops_);
    }

    MaybeFinish();
  }

  void Write(const Request* msg, WriteOptions options) override {
    if (start_corked_) {
      write_ops_.SendInitialMetadata(&context_->send_initial_metadata_,
                                     context_->initial_metadata_flags());
      start_corked_ = false;
    }

    if (options.is_last_message()) {
      options.set_buffer_hint();
      write_ops_.ClientSendClose();
    }
    // TODO(vjpai): don't assert
    GPR_CODEGEN_ASSERT(write_ops_.SendMessagePtr(msg, options).ok());
    callbacks_outstanding_++;
    if (started_) {
      call_.PerformOps(&write_ops_);
    } else {
      write_ops_at_start_ = true;
    }
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
    if (started_) {
      call_.PerformOps(&writes_done_ops_);
    } else {
      writes_done_ops_at_start_ = true;
    }
  }

 private:
  friend class ClientCallbackWriterFactory<Request>;

  template <class Response>
  ClientCallbackWriterImpl(
      Call call, ClientContext* context, Response* response,
      ::grpc::experimental::ClientWriteReactor<Request>* reactor)
      : context_(context),
        call_(call),
        reactor_(reactor),
        start_corked_(context_->initial_metadata_corked_) {
    this->BindReactor(reactor);
    finish_ops_.RecvMessage(response);
    finish_ops_.AllowNoMessage();
  }

  ClientContext* context_;
  Call call_;
  ::grpc::experimental::ClientWriteReactor<Request>* reactor_;

  CallOpSet<CallOpSendInitialMetadata, CallOpRecvInitialMetadata> start_ops_;
  CallbackWithSuccessTag start_tag_;
  bool start_corked_;

  CallOpSet<CallOpGenericRecvMessage, CallOpClientRecvStatus> finish_ops_;
  CallbackWithSuccessTag finish_tag_;
  Status finish_status_;

  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage, CallOpClientSendClose>
      write_ops_;
  CallbackWithSuccessTag write_tag_;
  bool write_ops_at_start_{false};

  CallOpSet<CallOpSendInitialMetadata, CallOpClientSendClose> writes_done_ops_;
  CallbackWithSuccessTag writes_done_tag_;
  bool writes_done_ops_at_start_{false};

  // Minimum of 3 callbacks to pre-register for StartCall, start, and finish
  std::atomic_int callbacks_outstanding_{3};
  bool started_{false};
};

template <class Request>
class ClientCallbackWriterFactory {
 public:
  template <class Response>
  static void Create(
      ChannelInterface* channel, const ::grpc::internal::RpcMethod& method,
      ClientContext* context, Response* response,
      ::grpc::experimental::ClientWriteReactor<Request>* reactor) {
    Call call = channel->CreateCall(method, context, channel->CallbackCQ());

    g_core_codegen_interface->grpc_call_ref(call.call());
    new (g_core_codegen_interface->grpc_call_arena_alloc(
        call.call(), sizeof(ClientCallbackWriterImpl<Request>)))
        ClientCallbackWriterImpl<Request>(call, context, response, reactor);
  }
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CLIENT_CALLBACK_H
