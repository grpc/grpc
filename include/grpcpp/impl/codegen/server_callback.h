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

#ifndef GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_H
#define GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_H

#include <atomic>
#include <functional>
#include <type_traits>

#include <grpcpp/impl/codegen/call.h>
#include <grpcpp/impl/codegen/call_op_set.h>
#include <grpcpp/impl/codegen/callback_common.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/server_context.h>
#include <grpcpp/impl/codegen/server_interface.h>
#include <grpcpp/impl/codegen/status.h>

namespace grpc {

// Declare base class of all reactors as internal
namespace internal {

class ServerReactor {
 public:
  virtual ~ServerReactor() = default;
  virtual void OnDone() {}
  virtual void OnCancel() {}
};

}  // namespace internal

namespace experimental {

// Forward declarations
template <class Request, class Response>
class ServerReadReactor;
template <class Request, class Response>
class ServerWriteReactor;
template <class Request, class Response>
class ServerBidiReactor;

// For unary RPCs, the exposed controller class is only an interface
// and the actual implementation is an internal class.
class ServerCallbackRpcController {
 public:
  virtual ~ServerCallbackRpcController() = default;

  // The method handler must call this function when it is done so that
  // the library knows to free its resources
  virtual void Finish(Status s) = 0;

  // Allow the method handler to push out the initial metadata before
  // the response and status are ready
  virtual void SendInitialMetadata(std::function<void(bool)>) = 0;
};

// NOTE: The actual streaming object classes are provided
// as API only to support mocking. There are no implementations of
// these class interfaces in the API.
template <class Request>
class ServerCallbackReader {
 public:
  virtual ~ServerCallbackReader() {}
  virtual void Finish(Status s) = 0;
  virtual void SendInitialMetadata() = 0;
  virtual void Read(Request* msg) = 0;

 protected:
  template <class Response>
  void BindReactor(ServerReadReactor<Request, Response>* reactor) {
    reactor->BindReader(this);
  }
};

template <class Response>
class ServerCallbackWriter {
 public:
  virtual ~ServerCallbackWriter() {}

  virtual void Finish(Status s) = 0;
  virtual void SendInitialMetadata() = 0;
  virtual void Write(const Response* msg, WriteOptions options) = 0;
  virtual void WriteAndFinish(const Response* msg, WriteOptions options,
                              Status s) {
    // Default implementation that can/should be overridden
    Write(msg, std::move(options));
    Finish(std::move(s));
  };

 protected:
  template <class Request>
  void BindReactor(ServerWriteReactor<Request, Response>* reactor) {
    reactor->BindWriter(this);
  }
};

template <class Request, class Response>
class ServerCallbackReaderWriter {
 public:
  virtual ~ServerCallbackReaderWriter() {}

  virtual void Finish(Status s) = 0;
  virtual void SendInitialMetadata() = 0;
  virtual void Read(Request* msg) = 0;
  virtual void Write(const Response* msg, WriteOptions options) = 0;
  virtual void WriteAndFinish(const Response* msg, WriteOptions options,
                              Status s) {
    // Default implementation that can/should be overridden
    Write(msg, std::move(options));
    Finish(std::move(s));
  };

 protected:
  void BindReactor(ServerBidiReactor<Request, Response>* reactor) {
    reactor->BindStream(this);
  }
};

// The following classes are reactors that are to be implemented
// by the user, returned as the result of the method handler for
// a callback method, and activated by the call to OnStarted
template <class Request, class Response>
class ServerBidiReactor : public internal::ServerReactor {
 public:
  ~ServerBidiReactor() = default;
  virtual void OnStarted(ServerContext*) {}
  virtual void OnSendInitialMetadataDone(bool ok) {}
  virtual void OnReadDone(bool ok) {}
  virtual void OnWriteDone(bool ok) {}

  void StartSendInitialMetadata() { stream_->SendInitialMetadata(); }
  void StartRead(Request* msg) { stream_->Read(msg); }
  void StartWrite(const Response* msg) { StartWrite(msg, WriteOptions()); }
  void StartWrite(const Response* msg, WriteOptions options) {
    stream_->Write(msg, std::move(options));
  }
  void StartWriteAndFinish(const Response* msg, WriteOptions options,
                           Status s) {
    stream_->WriteAndFinish(msg, std::move(options), std::move(s));
  }
  void StartWriteLast(const Response* msg, WriteOptions options) {
    StartWrite(msg, std::move(options.set_last_message()));
  }
  void Finish(Status s) { stream_->Finish(std::move(s)); }

 private:
  friend class ServerCallbackReaderWriter<Request, Response>;
  void BindStream(ServerCallbackReaderWriter<Request, Response>* stream) {
    stream_ = stream;
  }

  ServerCallbackReaderWriter<Request, Response>* stream_;
};

template <class Request, class Response>
class ServerReadReactor : public internal::ServerReactor {
 public:
  ~ServerReadReactor() = default;
  virtual void OnStarted(ServerContext*, Response* resp) {}
  virtual void OnSendInitialMetadataDone(bool ok) {}
  virtual void OnReadDone(bool ok) {}

  void StartSendInitialMetadata() { reader_->SendInitialMetadata(); }
  void StartRead(Request* msg) { reader_->Read(msg); }
  void Finish(Status s) { reader_->Finish(std::move(s)); }

 private:
  friend class ServerCallbackReader<Request>;
  void BindReader(ServerCallbackReader<Request>* reader) { reader_ = reader; }

  ServerCallbackReader<Request>* reader_;
};

template <class Request, class Response>
class ServerWriteReactor : public internal::ServerReactor {
 public:
  ~ServerWriteReactor() = default;
  virtual void OnStarted(ServerContext*, const Request* req) {}
  virtual void OnSendInitialMetadataDone(bool ok) {}
  virtual void OnWriteDone(bool ok) {}

  void StartSendInitialMetadata() { writer_->SendInitialMetadata(); }
  void StartWrite(const Response* msg) { StartWrite(msg, WriteOptions()); }
  void StartWrite(const Response* msg, WriteOptions options) {
    writer_->Write(msg, std::move(options));
  }
  void StartWriteAndFinish(const Response* msg, WriteOptions options,
                           Status s) {
    writer_->WriteAndFinish(msg, std::move(options), std::move(s));
  }
  void StartWriteLast(const Response* msg, WriteOptions options) {
    StartWrite(msg, std::move(options.set_last_message()));
  }
  void Finish(Status s) { writer_->Finish(std::move(s)); }

 private:
  friend class ServerCallbackWriter<Response>;
  void BindWriter(ServerCallbackWriter<Response>* writer) { writer_ = writer; }

  ServerCallbackWriter<Response>* writer_;
};

}  // namespace experimental

namespace internal {

template <class Request, class Response>
class UnimplementedReadReactor
    : public experimental::ServerReadReactor<Request, Response> {
 public:
  void OnDone() override { delete this; }
  void OnStarted(ServerContext*, Response*) override {
    this->Finish(Status(StatusCode::UNIMPLEMENTED, ""));
  }
};

template <class Request, class Response>
class UnimplementedWriteReactor
    : public experimental::ServerWriteReactor<Request, Response> {
 public:
  void OnDone() override { delete this; }
  void OnStarted(ServerContext*, const Request*) override {
    this->Finish(Status(StatusCode::UNIMPLEMENTED, ""));
  }
};

template <class Request, class Response>
class UnimplementedBidiReactor
    : public experimental::ServerBidiReactor<Request, Response> {
 public:
  void OnDone() override { delete this; }
  void OnStarted(ServerContext*) override {
    this->Finish(Status(StatusCode::UNIMPLEMENTED, ""));
  }
};

template <class RequestType, class ResponseType>
class CallbackUnaryHandler : public MethodHandler {
 public:
  CallbackUnaryHandler(
      std::function<void(ServerContext*, const RequestType*, ResponseType*,
                         experimental::ServerCallbackRpcController*)>
          func)
      : func_(func) {}
  void RunHandler(const HandlerParameter& param) final {
    // Arena allocate a controller structure (that includes request/response)
    g_core_codegen_interface->grpc_call_ref(param.call->call());
    auto* controller = new (g_core_codegen_interface->grpc_call_arena_alloc(
        param.call->call(), sizeof(ServerCallbackRpcControllerImpl)))
        ServerCallbackRpcControllerImpl(
            param.server_context, param.call,
            static_cast<RequestType*>(param.request),
            std::move(param.call_requester));
    Status status = param.status;

    if (status.ok()) {
      // Call the actual function handler and expect the user to call finish
      CatchingCallback(func_, param.server_context, controller->request(),
                       controller->response(), controller);
    } else {
      // if deserialization failed, we need to fail the call
      controller->Finish(status);
    }
  }

  void* Deserialize(grpc_call* call, grpc_byte_buffer* req,
                    Status* status) final {
    ByteBuffer buf;
    buf.set_buffer(req);
    auto* request = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call, sizeof(RequestType))) RequestType();
    *status = SerializationTraits<RequestType>::Deserialize(&buf, request);
    buf.Release();
    if (status->ok()) {
      return request;
    }
    request->~RequestType();
    return nullptr;
  }

 private:
  std::function<void(ServerContext*, const RequestType*, ResponseType*,
                     experimental::ServerCallbackRpcController*)>
      func_;

  // The implementation class of ServerCallbackRpcController is a private member
  // of CallbackUnaryHandler since it is never exposed anywhere, and this allows
  // it to take advantage of CallbackUnaryHandler's friendships.
  class ServerCallbackRpcControllerImpl
      : public experimental::ServerCallbackRpcController {
   public:
    void Finish(Status s) override {
      finish_tag_.Set(call_.call(), [this](bool) { MaybeDone(); },
                      &finish_ops_);
      if (!ctx_->sent_initial_metadata_) {
        finish_ops_.SendInitialMetadata(&ctx_->initial_metadata_,
                                        ctx_->initial_metadata_flags());
        if (ctx_->compression_level_set()) {
          finish_ops_.set_compression_level(ctx_->compression_level());
        }
        ctx_->sent_initial_metadata_ = true;
      }
      // The response is dropped if the status is not OK.
      if (s.ok()) {
        finish_ops_.ServerSendStatus(&ctx_->trailing_metadata_,
                                     finish_ops_.SendMessagePtr(&resp_));
      } else {
        finish_ops_.ServerSendStatus(&ctx_->trailing_metadata_, s);
      }
      finish_ops_.set_core_cq_tag(&finish_tag_);
      call_.PerformOps(&finish_ops_);
    }

    void SendInitialMetadata(std::function<void(bool)> f) override {
      GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);
      callbacks_outstanding_++;
      // TODO(vjpai): Consider taking f as a move-capture if we adopt C++14
      //              and if performance of this operation matters
      meta_tag_.Set(call_.call(),
                    [this, f](bool ok) {
                      f(ok);
                      MaybeDone();
                    },
                    &meta_ops_);
      meta_ops_.SendInitialMetadata(&ctx_->initial_metadata_,
                                    ctx_->initial_metadata_flags());
      if (ctx_->compression_level_set()) {
        meta_ops_.set_compression_level(ctx_->compression_level());
      }
      ctx_->sent_initial_metadata_ = true;
      meta_ops_.set_core_cq_tag(&meta_tag_);
      call_.PerformOps(&meta_ops_);
    }

   private:
    friend class CallbackUnaryHandler<RequestType, ResponseType>;

    ServerCallbackRpcControllerImpl(ServerContext* ctx, Call* call,
                                    const RequestType* req,
                                    std::function<void()> call_requester)
        : ctx_(ctx),
          call_(*call),
          req_(req),
          call_requester_(std::move(call_requester)) {
      ctx_->BeginCompletionOp(call, [this](bool) { MaybeDone(); }, nullptr);
    }

    ~ServerCallbackRpcControllerImpl() { req_->~RequestType(); }

    const RequestType* request() { return req_; }
    ResponseType* response() { return &resp_; }

    void MaybeDone() {
      if (--callbacks_outstanding_ == 0) {
        grpc_call* call = call_.call();
        auto call_requester = std::move(call_requester_);
        this->~ServerCallbackRpcControllerImpl();  // explicitly call destructor
        g_core_codegen_interface->grpc_call_unref(call);
        call_requester();
      }
    }

    CallOpSet<CallOpSendInitialMetadata> meta_ops_;
    CallbackWithSuccessTag meta_tag_;
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpServerSendStatus>
        finish_ops_;
    CallbackWithSuccessTag finish_tag_;

    ServerContext* ctx_;
    Call call_;
    const RequestType* req_;
    ResponseType resp_;
    std::function<void()> call_requester_;
    std::atomic_int callbacks_outstanding_{
        2};  // reserve for Finish and CompletionOp
  };
};

template <class RequestType, class ResponseType>
class CallbackClientStreamingHandler : public MethodHandler {
 public:
  CallbackClientStreamingHandler(
      std::function<
          experimental::ServerReadReactor<RequestType, ResponseType>*()>
          func)
      : func_(std::move(func)) {}
  void RunHandler(const HandlerParameter& param) final {
    // Arena allocate a reader structure (that includes response)
    g_core_codegen_interface->grpc_call_ref(param.call->call());

    experimental::ServerReadReactor<RequestType, ResponseType>* reactor =
        param.status.ok()
            ? CatchingReactorCreator<
                  experimental::ServerReadReactor<RequestType, ResponseType>>(
                  func_)
            : nullptr;

    if (reactor == nullptr) {
      // if deserialization or reactor creator failed, we need to fail the call
      reactor = new UnimplementedReadReactor<RequestType, ResponseType>;
    }

    auto* reader = new (g_core_codegen_interface->grpc_call_arena_alloc(
        param.call->call(), sizeof(ServerCallbackReaderImpl)))
        ServerCallbackReaderImpl(param.server_context, param.call,
                                 std::move(param.call_requester), reactor);

    reader->BindReactor(reactor);
    reactor->OnStarted(param.server_context, reader->response());
    reader->MaybeDone();
  }

 private:
  std::function<experimental::ServerReadReactor<RequestType, ResponseType>*()>
      func_;

  class ServerCallbackReaderImpl
      : public experimental::ServerCallbackReader<RequestType> {
   public:
    void Finish(Status s) override {
      finish_tag_.Set(call_.call(), [this](bool) { MaybeDone(); },
                      &finish_ops_);
      if (!ctx_->sent_initial_metadata_) {
        finish_ops_.SendInitialMetadata(&ctx_->initial_metadata_,
                                        ctx_->initial_metadata_flags());
        if (ctx_->compression_level_set()) {
          finish_ops_.set_compression_level(ctx_->compression_level());
        }
        ctx_->sent_initial_metadata_ = true;
      }
      // The response is dropped if the status is not OK.
      if (s.ok()) {
        finish_ops_.ServerSendStatus(&ctx_->trailing_metadata_,
                                     finish_ops_.SendMessagePtr(&resp_));
      } else {
        finish_ops_.ServerSendStatus(&ctx_->trailing_metadata_, s);
      }
      finish_ops_.set_core_cq_tag(&finish_tag_);
      call_.PerformOps(&finish_ops_);
    }

    void SendInitialMetadata() override {
      GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);
      callbacks_outstanding_++;
      meta_tag_.Set(call_.call(),
                    [this](bool ok) {
                      reactor_->OnSendInitialMetadataDone(ok);
                      MaybeDone();
                    },
                    &meta_ops_);
      meta_ops_.SendInitialMetadata(&ctx_->initial_metadata_,
                                    ctx_->initial_metadata_flags());
      if (ctx_->compression_level_set()) {
        meta_ops_.set_compression_level(ctx_->compression_level());
      }
      ctx_->sent_initial_metadata_ = true;
      meta_ops_.set_core_cq_tag(&meta_tag_);
      call_.PerformOps(&meta_ops_);
    }

    void Read(RequestType* req) override {
      callbacks_outstanding_++;
      read_ops_.RecvMessage(req);
      call_.PerformOps(&read_ops_);
    }

   private:
    friend class CallbackClientStreamingHandler<RequestType, ResponseType>;

    ServerCallbackReaderImpl(
        ServerContext* ctx, Call* call, std::function<void()> call_requester,
        experimental::ServerReadReactor<RequestType, ResponseType>* reactor)
        : ctx_(ctx),
          call_(*call),
          call_requester_(std::move(call_requester)),
          reactor_(reactor) {
      ctx_->BeginCompletionOp(call, [this](bool) { MaybeDone(); }, reactor);
      read_tag_.Set(call_.call(),
                    [this](bool ok) {
                      reactor_->OnReadDone(ok);
                      MaybeDone();
                    },
                    &read_ops_);
      read_ops_.set_core_cq_tag(&read_tag_);
    }

    ~ServerCallbackReaderImpl() {}

    ResponseType* response() { return &resp_; }

    void MaybeDone() {
      if (--callbacks_outstanding_ == 0) {
        reactor_->OnDone();
        grpc_call* call = call_.call();
        auto call_requester = std::move(call_requester_);
        this->~ServerCallbackReaderImpl();  // explicitly call destructor
        g_core_codegen_interface->grpc_call_unref(call);
        call_requester();
      }
    }

    CallOpSet<CallOpSendInitialMetadata> meta_ops_;
    CallbackWithSuccessTag meta_tag_;
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpServerSendStatus>
        finish_ops_;
    CallbackWithSuccessTag finish_tag_;
    CallOpSet<CallOpRecvMessage<RequestType>> read_ops_;
    CallbackWithSuccessTag read_tag_;

    ServerContext* ctx_;
    Call call_;
    ResponseType resp_;
    std::function<void()> call_requester_;
    experimental::ServerReadReactor<RequestType, ResponseType>* reactor_;
    std::atomic_int callbacks_outstanding_{
        3};  // reserve for OnStarted, Finish, and CompletionOp
  };
};

template <class RequestType, class ResponseType>
class CallbackServerStreamingHandler : public MethodHandler {
 public:
  CallbackServerStreamingHandler(
      std::function<
          experimental::ServerWriteReactor<RequestType, ResponseType>*()>
          func)
      : func_(std::move(func)) {}
  void RunHandler(const HandlerParameter& param) final {
    // Arena allocate a writer structure
    g_core_codegen_interface->grpc_call_ref(param.call->call());

    experimental::ServerWriteReactor<RequestType, ResponseType>* reactor =
        param.status.ok()
            ? CatchingReactorCreator<
                  experimental::ServerWriteReactor<RequestType, ResponseType>>(
                  func_)
            : nullptr;

    if (reactor == nullptr) {
      // if deserialization or reactor creator failed, we need to fail the call
      reactor = new UnimplementedWriteReactor<RequestType, ResponseType>;
    }

    auto* writer = new (g_core_codegen_interface->grpc_call_arena_alloc(
        param.call->call(), sizeof(ServerCallbackWriterImpl)))
        ServerCallbackWriterImpl(param.server_context, param.call,
                                 static_cast<RequestType*>(param.request),
                                 std::move(param.call_requester), reactor);
    writer->BindReactor(reactor);
    reactor->OnStarted(param.server_context, writer->request());
    writer->MaybeDone();
  }

  void* Deserialize(grpc_call* call, grpc_byte_buffer* req,
                    Status* status) final {
    ByteBuffer buf;
    buf.set_buffer(req);
    auto* request = new (g_core_codegen_interface->grpc_call_arena_alloc(
        call, sizeof(RequestType))) RequestType();
    *status = SerializationTraits<RequestType>::Deserialize(&buf, request);
    buf.Release();
    if (status->ok()) {
      return request;
    }
    request->~RequestType();
    return nullptr;
  }

 private:
  std::function<experimental::ServerWriteReactor<RequestType, ResponseType>*()>
      func_;

  class ServerCallbackWriterImpl
      : public experimental::ServerCallbackWriter<ResponseType> {
   public:
    void Finish(Status s) override {
      finish_tag_.Set(call_.call(), [this](bool) { MaybeDone(); },
                      &finish_ops_);
      finish_ops_.set_core_cq_tag(&finish_tag_);

      if (!ctx_->sent_initial_metadata_) {
        finish_ops_.SendInitialMetadata(&ctx_->initial_metadata_,
                                        ctx_->initial_metadata_flags());
        if (ctx_->compression_level_set()) {
          finish_ops_.set_compression_level(ctx_->compression_level());
        }
        ctx_->sent_initial_metadata_ = true;
      }
      finish_ops_.ServerSendStatus(&ctx_->trailing_metadata_, s);
      call_.PerformOps(&finish_ops_);
    }

    void SendInitialMetadata() override {
      GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);
      callbacks_outstanding_++;
      meta_tag_.Set(call_.call(),
                    [this](bool ok) {
                      reactor_->OnSendInitialMetadataDone(ok);
                      MaybeDone();
                    },
                    &meta_ops_);
      meta_ops_.SendInitialMetadata(&ctx_->initial_metadata_,
                                    ctx_->initial_metadata_flags());
      if (ctx_->compression_level_set()) {
        meta_ops_.set_compression_level(ctx_->compression_level());
      }
      ctx_->sent_initial_metadata_ = true;
      meta_ops_.set_core_cq_tag(&meta_tag_);
      call_.PerformOps(&meta_ops_);
    }

    void Write(const ResponseType* resp, WriteOptions options) override {
      callbacks_outstanding_++;
      if (options.is_last_message()) {
        options.set_buffer_hint();
      }
      if (!ctx_->sent_initial_metadata_) {
        write_ops_.SendInitialMetadata(&ctx_->initial_metadata_,
                                       ctx_->initial_metadata_flags());
        if (ctx_->compression_level_set()) {
          write_ops_.set_compression_level(ctx_->compression_level());
        }
        ctx_->sent_initial_metadata_ = true;
      }
      // TODO(vjpai): don't assert
      GPR_CODEGEN_ASSERT(write_ops_.SendMessagePtr(resp, options).ok());
      call_.PerformOps(&write_ops_);
    }

    void WriteAndFinish(const ResponseType* resp, WriteOptions options,
                        Status s) override {
      // This combines the write into the finish callback
      // Don't send any message if the status is bad
      if (s.ok()) {
        // TODO(vjpai): don't assert
        GPR_CODEGEN_ASSERT(finish_ops_.SendMessagePtr(resp, options).ok());
      }
      Finish(std::move(s));
    }

   private:
    friend class CallbackServerStreamingHandler<RequestType, ResponseType>;

    ServerCallbackWriterImpl(
        ServerContext* ctx, Call* call, const RequestType* req,
        std::function<void()> call_requester,
        experimental::ServerWriteReactor<RequestType, ResponseType>* reactor)
        : ctx_(ctx),
          call_(*call),
          req_(req),
          call_requester_(std::move(call_requester)),
          reactor_(reactor) {
      ctx_->BeginCompletionOp(call, [this](bool) { MaybeDone(); }, reactor);
      write_tag_.Set(call_.call(),
                     [this](bool ok) {
                       reactor_->OnWriteDone(ok);
                       MaybeDone();
                     },
                     &write_ops_);
      write_ops_.set_core_cq_tag(&write_tag_);
    }
    ~ServerCallbackWriterImpl() { req_->~RequestType(); }

    const RequestType* request() { return req_; }

    void MaybeDone() {
      if (--callbacks_outstanding_ == 0) {
        reactor_->OnDone();
        grpc_call* call = call_.call();
        auto call_requester = std::move(call_requester_);
        this->~ServerCallbackWriterImpl();  // explicitly call destructor
        g_core_codegen_interface->grpc_call_unref(call);
        call_requester();
      }
    }

    CallOpSet<CallOpSendInitialMetadata> meta_ops_;
    CallbackWithSuccessTag meta_tag_;
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpServerSendStatus>
        finish_ops_;
    CallbackWithSuccessTag finish_tag_;
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage> write_ops_;
    CallbackWithSuccessTag write_tag_;

    ServerContext* ctx_;
    Call call_;
    const RequestType* req_;
    std::function<void()> call_requester_;
    experimental::ServerWriteReactor<RequestType, ResponseType>* reactor_;
    std::atomic_int callbacks_outstanding_{
        3};  // reserve for OnStarted, Finish, and CompletionOp
  };
};

template <class RequestType, class ResponseType>
class CallbackBidiHandler : public MethodHandler {
 public:
  CallbackBidiHandler(
      std::function<
          experimental::ServerBidiReactor<RequestType, ResponseType>*()>
          func)
      : func_(std::move(func)) {}
  void RunHandler(const HandlerParameter& param) final {
    g_core_codegen_interface->grpc_call_ref(param.call->call());

    experimental::ServerBidiReactor<RequestType, ResponseType>* reactor =
        param.status.ok()
            ? CatchingReactorCreator<
                  experimental::ServerBidiReactor<RequestType, ResponseType>>(
                  func_)
            : nullptr;

    if (reactor == nullptr) {
      // if deserialization or reactor creator failed, we need to fail the call
      reactor = new UnimplementedBidiReactor<RequestType, ResponseType>;
    }

    auto* stream = new (g_core_codegen_interface->grpc_call_arena_alloc(
        param.call->call(), sizeof(ServerCallbackReaderWriterImpl)))
        ServerCallbackReaderWriterImpl(param.server_context, param.call,
                                       std::move(param.call_requester),
                                       reactor);

    stream->BindReactor(reactor);
    reactor->OnStarted(param.server_context);
    stream->MaybeDone();
  }

 private:
  std::function<experimental::ServerBidiReactor<RequestType, ResponseType>*()>
      func_;

  class ServerCallbackReaderWriterImpl
      : public experimental::ServerCallbackReaderWriter<RequestType,
                                                        ResponseType> {
   public:
    void Finish(Status s) override {
      finish_tag_.Set(call_.call(), [this](bool) { MaybeDone(); },
                      &finish_ops_);
      finish_ops_.set_core_cq_tag(&finish_tag_);

      if (!ctx_->sent_initial_metadata_) {
        finish_ops_.SendInitialMetadata(&ctx_->initial_metadata_,
                                        ctx_->initial_metadata_flags());
        if (ctx_->compression_level_set()) {
          finish_ops_.set_compression_level(ctx_->compression_level());
        }
        ctx_->sent_initial_metadata_ = true;
      }
      finish_ops_.ServerSendStatus(&ctx_->trailing_metadata_, s);
      call_.PerformOps(&finish_ops_);
    }

    void SendInitialMetadata() override {
      GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);
      callbacks_outstanding_++;
      meta_tag_.Set(call_.call(),
                    [this](bool ok) {
                      reactor_->OnSendInitialMetadataDone(ok);
                      MaybeDone();
                    },
                    &meta_ops_);
      meta_ops_.SendInitialMetadata(&ctx_->initial_metadata_,
                                    ctx_->initial_metadata_flags());
      if (ctx_->compression_level_set()) {
        meta_ops_.set_compression_level(ctx_->compression_level());
      }
      ctx_->sent_initial_metadata_ = true;
      meta_ops_.set_core_cq_tag(&meta_tag_);
      call_.PerformOps(&meta_ops_);
    }

    void Write(const ResponseType* resp, WriteOptions options) override {
      callbacks_outstanding_++;
      if (options.is_last_message()) {
        options.set_buffer_hint();
      }
      if (!ctx_->sent_initial_metadata_) {
        write_ops_.SendInitialMetadata(&ctx_->initial_metadata_,
                                       ctx_->initial_metadata_flags());
        if (ctx_->compression_level_set()) {
          write_ops_.set_compression_level(ctx_->compression_level());
        }
        ctx_->sent_initial_metadata_ = true;
      }
      // TODO(vjpai): don't assert
      GPR_CODEGEN_ASSERT(write_ops_.SendMessagePtr(resp, options).ok());
      call_.PerformOps(&write_ops_);
    }

    void WriteAndFinish(const ResponseType* resp, WriteOptions options,
                        Status s) override {
      // Don't send any message if the status is bad
      if (s.ok()) {
        // TODO(vjpai): don't assert
        GPR_CODEGEN_ASSERT(finish_ops_.SendMessagePtr(resp, options).ok());
      }
      Finish(std::move(s));
    }

    void Read(RequestType* req) override {
      callbacks_outstanding_++;
      read_ops_.RecvMessage(req);
      call_.PerformOps(&read_ops_);
    }

   private:
    friend class CallbackBidiHandler<RequestType, ResponseType>;

    ServerCallbackReaderWriterImpl(
        ServerContext* ctx, Call* call, std::function<void()> call_requester,
        experimental::ServerBidiReactor<RequestType, ResponseType>* reactor)
        : ctx_(ctx),
          call_(*call),
          call_requester_(std::move(call_requester)),
          reactor_(reactor) {
      ctx_->BeginCompletionOp(call, [this](bool) { MaybeDone(); }, reactor);
      write_tag_.Set(call_.call(),
                     [this](bool ok) {
                       reactor_->OnWriteDone(ok);
                       MaybeDone();
                     },
                     &write_ops_);
      write_ops_.set_core_cq_tag(&write_tag_);
      read_tag_.Set(call_.call(),
                    [this](bool ok) {
                      reactor_->OnReadDone(ok);
                      MaybeDone();
                    },
                    &read_ops_);
      read_ops_.set_core_cq_tag(&read_tag_);
    }
    ~ServerCallbackReaderWriterImpl() {}

    void MaybeDone() {
      if (--callbacks_outstanding_ == 0) {
        reactor_->OnDone();
        grpc_call* call = call_.call();
        auto call_requester = std::move(call_requester_);
        this->~ServerCallbackReaderWriterImpl();  // explicitly call destructor
        g_core_codegen_interface->grpc_call_unref(call);
        call_requester();
      }
    }

    CallOpSet<CallOpSendInitialMetadata> meta_ops_;
    CallbackWithSuccessTag meta_tag_;
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpServerSendStatus>
        finish_ops_;
    CallbackWithSuccessTag finish_tag_;
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage> write_ops_;
    CallbackWithSuccessTag write_tag_;
    CallOpSet<CallOpRecvMessage<RequestType>> read_ops_;
    CallbackWithSuccessTag read_tag_;

    ServerContext* ctx_;
    Call call_;
    std::function<void()> call_requester_;
    experimental::ServerBidiReactor<RequestType, ResponseType>* reactor_;
    std::atomic_int callbacks_outstanding_{
        3};  // reserve for OnStarted, Finish, and CompletionOp
  };
};

}  // namespace internal

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_H
