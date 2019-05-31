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
#include <grpcpp/impl/codegen/message_allocator.h>
#include <grpcpp/impl/codegen/server_context.h>
#include <grpcpp/impl/codegen/server_interface.h>
#include <grpcpp/impl/codegen/status.h>

namespace grpc {

// Declare base class of all reactors as internal
namespace internal {

// Forward declarations
template <class Request, class Response>
class CallbackUnaryHandler;
template <class Request, class Response>
class CallbackClientStreamingHandler;
template <class Request, class Response>
class CallbackServerStreamingHandler;
template <class Request, class Response>
class CallbackBidiHandler;

class ServerReactor {
 public:
  virtual ~ServerReactor() = default;
  virtual void OnDone() = 0;
  virtual void OnCancel() = 0;

 private:
  friend class ::grpc::ServerContext;
  template <class Request, class Response>
  friend class CallbackUnaryHandler;
  template <class Request, class Response>
  friend class CallbackClientStreamingHandler;
  template <class Request, class Response>
  friend class CallbackServerStreamingHandler;
  template <class Request, class Response>
  friend class CallbackBidiHandler;

  // The ServerReactor is responsible for tracking when it is safe to call
  // OnCancel. This function should not be called until after OnStarted is done
  // and the RPC has completed with a cancellation. This is tracked by counting
  // how many of these conditions have been met and calling OnCancel when none
  // remain unmet.

  void MaybeCallOnCancel() {
    if (on_cancel_conditions_remaining_.fetch_sub(
            1, std::memory_order_acq_rel) == 1) {
      OnCancel();
    }
  }

  std::atomic_int on_cancel_conditions_remaining_{2};
};

template <class Request, class Response>
class DefaultMessageHolder
    : public experimental::MessageHolder<Request, Response> {
 public:
  DefaultMessageHolder() {
    this->set_request(&request_obj_);
    this->set_response(&response_obj_);
  }
  void Release() override {
    // the object is allocated in the call arena.
    this->~DefaultMessageHolder<Request, Response>();
  }

 private:
  Request request_obj_;
  Response response_obj_;
};

}  // namespace internal

namespace experimental {

// Forward declarations
template <class Request, class Response>
class ServerUnaryReactor;
template <class Request, class Response>
class ServerReadReactor;
template <class Request, class Response>
class ServerWriteReactor;
template <class Request, class Response>
class ServerBidiReactor;

// NOTE: The actual call/stream object classes are provided as API only to
// support mocking. There are no implementations of these class interfaces in
// the API.
class ServerCallbackUnary {
 public:
  virtual ~ServerCallbackUnary() {}
  virtual void Finish(Status s) = 0;
  virtual void SendInitialMetadata() = 0;
  virtual RpcAllocatorState* GetRpcAllocatorState() = 0;

 protected:
  template <class Request, class Response>
  void BindReactor(ServerUnaryReactor<Request, Response>* reactor) {
    reactor->BindCall(this);
  }
};

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
  }

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
  }

 protected:
  void BindReactor(ServerBidiReactor<Request, Response>* reactor) {
    reactor->BindStream(this);
  }
};

// The following classes are the reactor interfaces that are to be implemented
// by the user, returned as the result of the method handler for a callback
// method, and activated by the call to OnStarted. The library guarantees that
// OnStarted will be called for any reactor that has been created using a
// method handler registered on a service. No operation initiation method may be
// called until after the call to OnStarted.
// Note that none of the classes are pure; all reactions have a default empty
// reaction so that the user class only needs to override those classes that it
// cares about.

/// \a ServerBidiReactor is the interface for a bidirectional streaming RPC.
template <class Request, class Response>
class ServerBidiReactor : public internal::ServerReactor {
 public:
  ~ServerBidiReactor() = default;

  /// Do NOT call any operation initiation method (names that start with Start)
  /// until after the library has called OnStarted on this object.

  /// Send any initial metadata stored in the RPC context. If not invoked,
  /// any initial metadata will be passed along with the first Write or the
  /// Finish (if there are no writes).
  void StartSendInitialMetadata() { stream_->SendInitialMetadata(); }

  /// Initiate a read operation.
  ///
  /// \param[out] req Where to eventually store the read message. Valid when
  ///                 the library calls OnReadDone
  void StartRead(Request* req) { stream_->Read(req); }

  /// Initiate a write operation.
  ///
  /// \param[in] resp The message to be written. The library takes temporary
  ///                 ownership until OnWriteDone, at which point the
  ///                 application regains ownership of resp.
  void StartWrite(const Response* resp) { StartWrite(resp, WriteOptions()); }

  /// Initiate a write operation with specified options.
  ///
  /// \param[in] resp The message to be written. The library takes temporary
  ///                 ownership until OnWriteDone, at which point the
  ///                 application regains ownership of resp.
  /// \param[in] options The WriteOptions to use for writing this message
  void StartWrite(const Response* resp, WriteOptions options) {
    stream_->Write(resp, std::move(options));
  }

  /// Initiate a write operation with specified options and final RPC Status,
  /// which also causes any trailing metadata for this RPC to be sent out.
  /// StartWriteAndFinish is like merging StartWriteLast and Finish into a
  /// single step. A key difference, though, is that this operation doesn't have
  /// an OnWriteDone reaction - it is considered complete only when OnDone is
  /// available. An RPC can either have StartWriteAndFinish or Finish, but not
  /// both.
  ///
  /// \param[in] resp The message to be written. The library takes temporary
  ///                 ownership until Onone, at which point the application
  ///                 regains ownership of resp.
  /// \param[in] options The WriteOptions to use for writing this message
  /// \param[in] s The status outcome of this RPC
  void StartWriteAndFinish(const Response* resp, WriteOptions options,
                           Status s) {
    stream_->WriteAndFinish(resp, std::move(options), std::move(s));
  }

  /// Inform system of a planned write operation with specified options, but
  /// allow the library to schedule the actual write coalesced with the writing
  /// of trailing metadata (which takes place on a Finish call).
  ///
  /// \param[in] resp The message to be written. The library takes temporary
  ///                 ownership until OnWriteDone, at which point the
  ///                 application regains ownership of resp.
  /// \param[in] options The WriteOptions to use for writing this message
  void StartWriteLast(const Response* resp, WriteOptions options) {
    StartWrite(resp, std::move(options.set_last_message()));
  }

  /// Indicate that the stream is to be finished and the trailing metadata and
  /// RPC status are to be sent. Every RPC MUST be finished using either Finish
  /// or StartWriteAndFinish (but not both), even if the RPC is already
  /// cancelled.
  ///
  /// \param[in] s The status outcome of this RPC
  void Finish(Status s) { stream_->Finish(std::move(s)); }

  /// Notify the application that a streaming RPC has started and that it is now
  /// ok to call any operation initiation method. An RPC is considered started
  /// after the server has received all initial metadata from the client, which
  /// is a result of the client calling StartCall().
  virtual void OnStarted() {}

  /// Notifies the application that an explicit StartSendInitialMetadata
  /// operation completed. Not used when the sending of initial metadata
  /// piggybacks onto the first write.
  ///
  /// \param[in] ok Was it successful? If false, no further write-side operation
  ///               will succeed.
  virtual void OnSendInitialMetadataDone(bool ok) {}

  /// Notifies the application that a StartRead operation completed.
  ///
  /// \param[in] ok Was it successful? If false, no further read-side operation
  ///               will succeed.
  virtual void OnReadDone(bool ok) {}

  /// Notifies the application that a StartWrite (or StartWriteLast) operation
  /// completed.
  ///
  /// \param[in] ok Was it successful? If false, no further write-side operation
  ///               will succeed.
  virtual void OnWriteDone(bool ok) {}

  /// Notifies the application that all operations associated with this RPC
  /// have completed. This is an override (from the internal base class) but not
  /// final, so derived classes should override it if they want to take action.
  void OnDone() override {}

  /// Notifies the application that this RPC has been cancelled. This is an
  /// override (from the internal base class) but not final, so derived classes
  /// should override it if they want to take action.
  void OnCancel() override {}

 private:
  friend class ServerCallbackReaderWriter<Request, Response>;
  void BindStream(ServerCallbackReaderWriter<Request, Response>* stream) {
    stream_ = stream;
  }

  ServerCallbackReaderWriter<Request, Response>* stream_;
};

/// \a ServerReadReactor is the interface for a client-streaming RPC.
template <class Request, class Response>
class ServerReadReactor : public internal::ServerReactor {
 public:
  ~ServerReadReactor() = default;

  /// The following operation initiations are exactly like ServerBidiReactor.
  void StartSendInitialMetadata() { reader_->SendInitialMetadata(); }
  void StartRead(Request* req) { reader_->Read(req); }
  void Finish(Status s) { reader_->Finish(std::move(s)); }

  /// Similar to ServerBidiReactor::OnStarted, except that this also provides
  /// the response object that the stream fills in before calling Finish.
  /// (It must be filled in if status is OK, but it may be filled in otherwise.)
  ///
  /// \param[in] resp The response object to be used by this RPC
  virtual void OnStarted(Response* resp) {}

  /// The following notifications are exactly like ServerBidiReactor.
  virtual void OnSendInitialMetadataDone(bool ok) {}
  virtual void OnReadDone(bool ok) {}
  void OnDone() override {}
  void OnCancel() override {}

 private:
  friend class ServerCallbackReader<Request>;
  void BindReader(ServerCallbackReader<Request>* reader) { reader_ = reader; }

  ServerCallbackReader<Request>* reader_;
};

/// \a ServerWriteReactor is the interface for a server-streaming RPC.
template <class Request, class Response>
class ServerWriteReactor : public internal::ServerReactor {
 public:
  ~ServerWriteReactor() = default;

  /// The following operation initiations are exactly like ServerBidiReactor.
  void StartSendInitialMetadata() { writer_->SendInitialMetadata(); }
  void StartWrite(const Response* resp) { StartWrite(resp, WriteOptions()); }
  void StartWrite(const Response* resp, WriteOptions options) {
    writer_->Write(resp, std::move(options));
  }
  void StartWriteAndFinish(const Response* resp, WriteOptions options,
                           Status s) {
    writer_->WriteAndFinish(resp, std::move(options), std::move(s));
  }
  void StartWriteLast(const Response* resp, WriteOptions options) {
    StartWrite(resp, std::move(options.set_last_message()));
  }
  void Finish(Status s) { writer_->Finish(std::move(s)); }

  /// Similar to ServerBidiReactor::OnStarted, except that this also provides
  /// the request object sent by the client.
  ///
  /// \param[in] req The request object sent by the client
  virtual void OnStarted(const Request* req) {}

  /// The following notifications are exactly like ServerBidiReactor.
  virtual void OnSendInitialMetadataDone(bool ok) {}
  virtual void OnWriteDone(bool ok) {}
  void OnDone() override {}
  void OnCancel() override {}

 private:
  friend class ServerCallbackWriter<Response>;
  void BindWriter(ServerCallbackWriter<Response>* writer) { writer_ = writer; }

  ServerCallbackWriter<Response>* writer_;
};

/// \a ServerUnaryReactor is a reactor-based interface for a unary RPC.
template <class Request, class Response>
class ServerUnaryReactor : public internal::ServerReactor {
 public:
  ~ServerUnaryReactor() = default;

  /// The following operation initiations are exactly like ServerBidiReactor.
  void StartSendInitialMetadata() { call_->SendInitialMetadata(); }
  void Finish(Status s) { call_->Finish(std::move(s)); }

  /// Similar to ServerBidiReactor::OnStarted, except that this also provides
  /// the request object sent by the client and the response object that the
  /// stream fills in before calling Finish. (It must be filled in if status
  /// is OK, but it may be filled in otherwise.)
  ///
  /// \param[in] req The request object sent by the client
  /// \param[in] resp The response object to be used by this RPC
  virtual void OnStarted(const Request* req, Response* resp) {}

  /// The following notifications are exactly like ServerBidiReactor.
  virtual void OnSendInitialMetadataDone(bool ok) {}
  void OnDone() override {}
  void OnCancel() override {}

  // NOTE: This is an API for advanced users who need custom allocators.
  // Get and maybe mutate the allocator state associated with the current RPC.
  RpcAllocatorState* GetRpcAllocatorState() {
    return call_->GetRpcAllocatorState();
  }

 private:
  friend class ServerCallbackUnary;
  void BindCall(ServerCallbackUnary* call) { call_ = call; }

  ServerCallbackUnary* call_;
};

/// MakeReactor is a free function to make a simple ServerUnaryReactor that
/// causes the invocation of a function. There are two overloaded variants,
/// depending on the parameters and return type of the function being invoked.
/// The variant is selected using a simple template-based specialization.
///
/// \param context [in] The ServerContext created by the library for this RPC
/// \param func [in] A function that will be executed when this RPC is being
///        served. It can be of two acceptable signatures:
///           1. void(const Request*, Response*,
///                   ServerUnaryReactor<Request,Response>*)
///              In this form, func is responsible or invoking (or causing the
///              invocation of) Finish on the reactor in the 3rd argument.
///              This form gives maximum flexibility since the reactor can be
///              passed to other operations that may execute after a delay.
///           2. Status(const Request*, Response*)
///              In this form, func returns the Status of the RPC; the library
///              never exposes the reactor to func and directly Finish'es the
///              RPC with the Status returned. In this form, the function may
///              not execute any delaying operations (such as a child RPC).
///
/// \return A pointer to a ServerUnaryReactor<Request,Response> that executes
///         the given function when the RPC is invoked.
template <typename Request, typename Response, typename Function>
ServerUnaryReactor<Request, Response>* MakeReactor(
    ServerContext* context, Function&& func,
    typename std::enable_if<std::is_same<
    typename std::result_of<Function(const Request*, Response*, ServerUnaryReactor<Request, Response>*)>::type,
    void>::value>::type* = nullptr) {
  // TODO(vjpai): Specialize this to prevent counting OnCancel conditions
  class SimpleUnaryReactor final
      : public ServerUnaryReactor<Request, Response> {
   public:
    explicit SimpleUnaryReactor(Function&& func)
        : on_started_func_(std::move(func)) {}

   private:
    void OnStarted(const Request* req, Response* resp) override {
      on_started_func_(req, resp, this);
    }
    void OnDone() override { this->~SimpleUnaryReactor(); }

    Function on_started_func_;
  };
  return new (g_core_codegen_interface->grpc_call_arena_alloc(
      context->c_call(), sizeof(SimpleUnaryReactor)))
      SimpleUnaryReactor(std::forward<Function>(func));
}

template <typename Request, typename Response, typename Function>
ServerUnaryReactor<Request, Response>* MakeReactor(
    ServerContext* context, Function&& func,
    typename std::enable_if<std::is_same<
    typename std::result_of<Function(const Request*, Response*)>::type,
    Status>::value>::type* = nullptr) {
  // TODO(vjpai): Specialize this to prevent counting OnCancel conditions
  class ReallySimpleUnaryReactor final
      : public ServerUnaryReactor<Request, Response> {
   public:
    explicit ReallySimpleUnaryReactor(Function&& func)
        : on_started_func_(std::move(func)) {}

   private:
    void OnStarted(const Request* req, Response* resp) override {
      this->Finish(on_started_func_(req, resp));
    }
    void OnDone() override { this->~ReallySimpleUnaryReactor(); }

    Function on_started_func_;
  };
  return new (g_core_codegen_interface->grpc_call_arena_alloc(
      context->c_call(), sizeof(ReallySimpleUnaryReactor)))
      ReallySimpleUnaryReactor(std::forward<Function>(func));
}

}  // namespace experimental

namespace internal {

template <class Request, class Response>
class UnimplementedUnaryReactor
    : public experimental::ServerUnaryReactor<Request, Response> {
 public:
  void OnDone() override { delete this; }
  void OnStarted(const Request*, Response*) override {
    this->Finish(Status(StatusCode::UNIMPLEMENTED, ""));
  }
};

template <class Request, class Response>
class UnimplementedReadReactor
    : public experimental::ServerReadReactor<Request, Response> {
 public:
  void OnDone() override { delete this; }
  void OnStarted(Response*) override {
    this->Finish(Status(StatusCode::UNIMPLEMENTED, ""));
  }
};

template <class Request, class Response>
class UnimplementedWriteReactor
    : public experimental::ServerWriteReactor<Request, Response> {
 public:
  void OnDone() override { delete this; }
  void OnStarted(const Request*) override {
    this->Finish(Status(StatusCode::UNIMPLEMENTED, ""));
  }
};

template <class Request, class Response>
class UnimplementedBidiReactor
    : public experimental::ServerBidiReactor<Request, Response> {
 public:
  void OnDone() override { delete this; }
  void OnStarted() override {
    this->Finish(Status(StatusCode::UNIMPLEMENTED, ""));
  }
};

template <class RequestType, class ResponseType>
class CallbackUnaryHandler : public MethodHandler {
 public:
  explicit CallbackUnaryHandler(std::function<experimental::ServerUnaryReactor<
                                    RequestType, ResponseType>*(ServerContext*)>
                                    get_reactor)
      : get_reactor_(std::move(get_reactor)) {}

  void SetMessageAllocator(
      experimental::MessageAllocator<RequestType, ResponseType>* allocator) {
    allocator_ = allocator;
  }

  void RunHandler(const HandlerParameter& param) final {
    // Arena allocate a controller structure (that includes request/response)
    g_core_codegen_interface->grpc_call_ref(param.call->call());
    auto* allocator_state =
        static_cast<experimental::MessageHolder<RequestType, ResponseType>*>(
            param.internal_data);
    experimental::ServerUnaryReactor<RequestType, ResponseType>* reactor =
        param.status.ok()
            ? CatchingReactorCreator<
                  experimental::ServerUnaryReactor<RequestType, ResponseType>>(
                  get_reactor_, param.server_context)
            : nullptr;

    if (reactor == nullptr) {
      // if deserialization or reactor creator failed, we need to fail the call
      reactor = new UnimplementedUnaryReactor<RequestType, ResponseType>;
    }

    auto* call = new (g_core_codegen_interface->grpc_call_arena_alloc(
        param.call->call(), sizeof(ServerCallbackUnaryImpl)))
        ServerCallbackUnaryImpl(param.server_context, param.call,
                                allocator_state,
                                std::move(param.call_requester), reactor);

    call->BindReactor(reactor);
    reactor->OnStarted(call->request(), call->response());
    // The earliest that OnCancel can be called is after OnStarted is done.
    reactor->MaybeCallOnCancel();
    call->MaybeDone();
  }

  void* Deserialize(grpc_call* call, grpc_byte_buffer* req, Status* status,
                    void** handler_data) final {
    ByteBuffer buf;
    buf.set_buffer(req);
    RequestType* request = nullptr;
    experimental::MessageHolder<RequestType, ResponseType>* allocator_state =
        nullptr;
    if (allocator_ != nullptr) {
      allocator_state = allocator_->AllocateMessages();
    } else {
      allocator_state = new (g_core_codegen_interface->grpc_call_arena_alloc(
          call, sizeof(DefaultMessageHolder<RequestType, ResponseType>)))
          DefaultMessageHolder<RequestType, ResponseType>();
    }
    *handler_data = allocator_state;
    request = allocator_state->request();
    *status = SerializationTraits<RequestType>::Deserialize(&buf, request);
    buf.Release();
    if (status->ok()) {
      return request;
    }
    // Clean up on deserialization failure.
    allocator_state->Release();
    return nullptr;
  }

 private:
  std::function<experimental::ServerUnaryReactor<RequestType, ResponseType>*(
      ServerContext*)>
      get_reactor_;
  experimental::MessageAllocator<RequestType, ResponseType>* allocator_ =
      nullptr;

  class ServerCallbackUnaryImpl : public experimental::ServerCallbackUnary {
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
      // The response is dropped if the status is not OK.
      if (s.ok()) {
        finish_ops_.ServerSendStatus(&ctx_->trailing_metadata_,
                                     finish_ops_.SendMessagePtr(response()));
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

    experimental::RpcAllocatorState* GetRpcAllocatorState() override {
      return allocator_state_;
    }

   private:
    friend class CallbackUnaryHandler<RequestType, ResponseType>;

    ServerCallbackUnaryImpl(
        ServerContext* ctx, Call* call,
        experimental::MessageHolder<RequestType, ResponseType>* allocator_state,
        std::function<void()> call_requester,
        experimental::ServerUnaryReactor<RequestType, ResponseType>* reactor)
        : ctx_(ctx),
          call_(*call),
          allocator_state_(allocator_state),
          call_requester_(std::move(call_requester)),
          reactor_(reactor) {
      ctx_->BeginCompletionOp(call, [this](bool) { MaybeDone(); }, reactor);
    }

    const RequestType* request() { return allocator_state_->request(); }
    ResponseType* response() { return allocator_state_->response(); }

    void MaybeDone() {
      if (--callbacks_outstanding_ == 0) {
        reactor_->OnDone();
        grpc_call* call = call_.call();
        auto call_requester = std::move(call_requester_);
        allocator_state_->Release();
        this->~ServerCallbackUnaryImpl();  // explicitly call destructor
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
    experimental::MessageHolder<RequestType, ResponseType>* const
        allocator_state_;
    std::function<void()> call_requester_;
    experimental::ServerUnaryReactor<RequestType, ResponseType>* reactor_;
    std::atomic_int callbacks_outstanding_{
        3};  // reserve for OnStarted, Finish, and CompletionOp
  };
};

template <class RequestType, class ResponseType>
class CallbackClientStreamingHandler : public MethodHandler {
 public:
  explicit CallbackClientStreamingHandler(
      std::function<experimental::ServerReadReactor<RequestType, ResponseType>*(
          ServerContext*)>
          get_reactor)
      : get_reactor_(std::move(get_reactor)) {}
  void RunHandler(const HandlerParameter& param) final {
    // Arena allocate a reader structure (that includes response)
    g_core_codegen_interface->grpc_call_ref(param.call->call());

    experimental::ServerReadReactor<RequestType, ResponseType>* reactor =
        param.status.ok()
            ? CatchingReactorCreator<
                  experimental::ServerReadReactor<RequestType, ResponseType>>(
                  get_reactor_, param.server_context)
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
    reactor->OnStarted(reader->response());
    // The earliest that OnCancel can be called is after OnStarted is done.
    reactor->MaybeCallOnCancel();
    reader->MaybeDone();
  }

 private:
  std::function<experimental::ServerReadReactor<RequestType, ResponseType>*(
      ServerContext*)>
      get_reactor_;

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
  explicit CallbackServerStreamingHandler(
      std::function<experimental::ServerWriteReactor<
          RequestType, ResponseType>*(ServerContext*)>
          get_reactor)
      : get_reactor_(std::move(get_reactor)) {}
  void RunHandler(const HandlerParameter& param) final {
    // Arena allocate a writer structure
    g_core_codegen_interface->grpc_call_ref(param.call->call());

    experimental::ServerWriteReactor<RequestType, ResponseType>* reactor =
        param.status.ok()
            ? CatchingReactorCreator<
                  experimental::ServerWriteReactor<RequestType, ResponseType>>(
                  get_reactor_, param.server_context)
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
    reactor->OnStarted(writer->request());
    // The earliest that OnCancel can be called is after OnStarted is done.
    reactor->MaybeCallOnCancel();
    writer->MaybeDone();
  }

  void* Deserialize(grpc_call* call, grpc_byte_buffer* req, Status* status,
                    void** handler_data) final {
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
  std::function<experimental::ServerWriteReactor<RequestType, ResponseType>*(
      ServerContext*)>
      get_reactor_;

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
  explicit CallbackBidiHandler(
      std::function<experimental::ServerBidiReactor<RequestType, ResponseType>*(
          ServerContext*)>
          get_reactor)
      : get_reactor_(std::move(get_reactor)) {}
  void RunHandler(const HandlerParameter& param) final {
    g_core_codegen_interface->grpc_call_ref(param.call->call());

    experimental::ServerBidiReactor<RequestType, ResponseType>* reactor =
        param.status.ok()
            ? CatchingReactorCreator<
                  experimental::ServerBidiReactor<RequestType, ResponseType>>(
                  get_reactor_, param.server_context)
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
    reactor->OnStarted();
    // The earliest that OnCancel can be called is after OnStarted is done.
    reactor->MaybeCallOnCancel();
    stream->MaybeDone();
  }

 private:
  std::function<experimental::ServerBidiReactor<RequestType, ResponseType>*(
      ServerContext*)>
      get_reactor_;

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
