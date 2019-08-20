/*
 *
 * Copyright 2019 gRPC authors.
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
 */

#ifndef GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_IMPL_H
#define GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_IMPL_H

#include <atomic>
#include <functional>
#include <type_traits>

#include <grpcpp/impl/codegen/call.h>
#include <grpcpp/impl/codegen/call_op_set.h>
#include <grpcpp/impl/codegen/callback_common.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/message_allocator.h>
#include <grpcpp/impl/codegen/server_context_impl.h>
#include <grpcpp/impl/codegen/server_interface.h>
#include <grpcpp/impl/codegen/status.h>

namespace grpc_impl {

// Declare base class of all reactors as internal
namespace internal {

// Forward declarations
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
  friend class ::grpc_impl::ServerContext;
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
    if (GPR_UNLIKELY(on_cancel_conditions_remaining_.fetch_sub(
                         1, std::memory_order_acq_rel) == 1)) {
      OnCancel();
    }
  }

  std::atomic<intptr_t> on_cancel_conditions_remaining_{2};
};

template <class Request, class Response>
class DefaultMessageHolder
    : public ::grpc::experimental::MessageHolder<Request, Response> {
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
  virtual void Finish(::grpc::Status s) = 0;

  // Allow the method handler to push out the initial metadata before
  // the response and status are ready
  virtual void SendInitialMetadata(std::function<void(bool)>) = 0;

  /// SetCancelCallback passes in a callback to be called when the RPC is
  /// canceled for whatever reason (streaming calls have OnCancel instead). This
  /// is an advanced and uncommon use with several important restrictions. This
  /// function may not be called more than once on the same RPC.
  ///
  /// If code calls SetCancelCallback on an RPC, it must also call
  /// ClearCancelCallback before calling Finish on the RPC controller. That
  /// method makes sure that no cancellation callback is executed for this RPC
  /// beyond the point of its return. ClearCancelCallback may be called even if
  /// SetCancelCallback was not called for this RPC, and it may be called
  /// multiple times. It _must_ be called if SetCancelCallback was called for
  /// this RPC.
  ///
  /// The callback should generally be lightweight and nonblocking and primarily
  /// concerned with clearing application state related to the RPC or causing
  /// operations (such as cancellations) to happen on dependent RPCs.
  ///
  /// If the RPC is already canceled at the time that SetCancelCallback is
  /// called, the callback is invoked immediately.
  ///
  /// The cancellation callback may be executed concurrently with the method
  /// handler that invokes it but will certainly not issue or execute after the
  /// return of ClearCancelCallback. If ClearCancelCallback is invoked while the
  /// callback is already executing, the callback will complete its execution
  /// before ClearCancelCallback takes effect.
  ///
  /// To preserve the orderings described above, the callback may be called
  /// under a lock that is also used for ClearCancelCallback and
  /// ServerContext::IsCancelled, so the callback CANNOT call either of those
  /// operations on this RPC or any other function that causes those operations
  /// to be called before the callback completes.
  virtual void SetCancelCallback(std::function<void()> callback) = 0;
  virtual void ClearCancelCallback() = 0;

  // NOTE: This is an API for advanced users who need custom allocators.
  // Get and maybe mutate the allocator state associated with the current RPC.
  virtual grpc::experimental::RpcAllocatorState* GetRpcAllocatorState() = 0;
};

// NOTE: The actual streaming object classes are provided
// as API only to support mocking. There are no implementations of
// these class interfaces in the API.
template <class Request>
class ServerCallbackReader {
 public:
  virtual ~ServerCallbackReader() {}
  virtual void Finish(::grpc::Status s) = 0;
  virtual void SendInitialMetadata() = 0;
  virtual void Read(Request* msg) = 0;

 protected:
  template <class Response>
  void BindReactor(ServerReadReactor<Request, Response>* reactor) {
    reactor->InternalBindReader(this);
  }
};

template <class Response>
class ServerCallbackWriter {
 public:
  virtual ~ServerCallbackWriter() {}

  virtual void Finish(::grpc::Status s) = 0;
  virtual void SendInitialMetadata() = 0;
  virtual void Write(const Response* msg, ::grpc::WriteOptions options) = 0;
  virtual void WriteAndFinish(const Response* msg, ::grpc::WriteOptions options,
                              ::grpc::Status s) {
    // Default implementation that can/should be overridden
    Write(msg, std::move(options));
    Finish(std::move(s));
  }

 protected:
  template <class Request>
  void BindReactor(ServerWriteReactor<Request, Response>* reactor) {
    reactor->InternalBindWriter(this);
  }
};

template <class Request, class Response>
class ServerCallbackReaderWriter {
 public:
  virtual ~ServerCallbackReaderWriter() {}

  virtual void Finish(::grpc::Status s) = 0;
  virtual void SendInitialMetadata() = 0;
  virtual void Read(Request* msg) = 0;
  virtual void Write(const Response* msg, ::grpc::WriteOptions options) = 0;
  virtual void WriteAndFinish(const Response* msg, ::grpc::WriteOptions options,
                              ::grpc::Status s) {
    // Default implementation that can/should be overridden
    Write(msg, std::move(options));
    Finish(std::move(s));
  }

 protected:
  void BindReactor(ServerBidiReactor<Request, Response>* reactor) {
    reactor->InternalBindStream(this);
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
  void StartWrite(const Response* resp) {
    StartWrite(resp, ::grpc::WriteOptions());
  }

  /// Initiate a write operation with specified options.
  ///
  /// \param[in] resp The message to be written. The library takes temporary
  ///                 ownership until OnWriteDone, at which point the
  ///                 application regains ownership of resp.
  /// \param[in] options The WriteOptions to use for writing this message
  void StartWrite(const Response* resp, ::grpc::WriteOptions options) {
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
  void StartWriteAndFinish(const Response* resp, ::grpc::WriteOptions options,
                           ::grpc::Status s) {
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
  void StartWriteLast(const Response* resp, ::grpc::WriteOptions options) {
    StartWrite(resp, std::move(options.set_last_message()));
  }

  /// Indicate that the stream is to be finished and the trailing metadata and
  /// RPC status are to be sent. Every RPC MUST be finished using either Finish
  /// or StartWriteAndFinish (but not both), even if the RPC is already
  /// cancelled.
  ///
  /// \param[in] s The status outcome of this RPC
  void Finish(::grpc::Status s) { stream_->Finish(std::move(s)); }

  /// Notify the application that a streaming RPC has started and that it is now
  /// ok to call any operation initiation method. An RPC is considered started
  /// after the server has received all initial metadata from the client, which
  /// is a result of the client calling StartCall().
  ///
  /// \param[in] context The context object now associated with this RPC
  virtual void OnStarted(::grpc_impl::ServerContext* context) {}

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
  // May be overridden by internal implementation details. This is not a public
  // customization point.
  virtual void InternalBindStream(
      ServerCallbackReaderWriter<Request, Response>* stream) {
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
  void Finish(::grpc::Status s) { reader_->Finish(std::move(s)); }

  /// Similar to ServerBidiReactor::OnStarted, except that this also provides
  /// the response object that the stream fills in before calling Finish.
  /// (It must be filled in if status is OK, but it may be filled in otherwise.)
  ///
  /// \param[in] context The context object now associated with this RPC
  /// \param[in] resp The response object to be used by this RPC
  virtual void OnStarted(::grpc_impl::ServerContext* context, Response* resp) {}

  /// The following notifications are exactly like ServerBidiReactor.
  virtual void OnSendInitialMetadataDone(bool ok) {}
  virtual void OnReadDone(bool ok) {}
  void OnDone() override {}
  void OnCancel() override {}

 private:
  friend class ServerCallbackReader<Request>;
  // May be overridden by internal implementation details. This is not a public
  // customization point.
  virtual void InternalBindReader(ServerCallbackReader<Request>* reader) {
    reader_ = reader;
  }

  ServerCallbackReader<Request>* reader_;
};

/// \a ServerWriteReactor is the interface for a server-streaming RPC.
template <class Request, class Response>
class ServerWriteReactor : public internal::ServerReactor {
 public:
  ~ServerWriteReactor() = default;

  /// The following operation initiations are exactly like ServerBidiReactor.
  void StartSendInitialMetadata() { writer_->SendInitialMetadata(); }
  void StartWrite(const Response* resp) {
    StartWrite(resp, ::grpc::WriteOptions());
  }
  void StartWrite(const Response* resp, ::grpc::WriteOptions options) {
    writer_->Write(resp, std::move(options));
  }
  void StartWriteAndFinish(const Response* resp, ::grpc::WriteOptions options,
                           ::grpc::Status s) {
    writer_->WriteAndFinish(resp, std::move(options), std::move(s));
  }
  void StartWriteLast(const Response* resp, ::grpc::WriteOptions options) {
    StartWrite(resp, std::move(options.set_last_message()));
  }
  void Finish(::grpc::Status s) { writer_->Finish(std::move(s)); }

  /// Similar to ServerBidiReactor::OnStarted, except that this also provides
  /// the request object sent by the client.
  ///
  /// \param[in] context The context object now associated with this RPC
  /// \param[in] req The request object sent by the client
  virtual void OnStarted(::grpc_impl::ServerContext* context,
                         const Request* req) {}

  /// The following notifications are exactly like ServerBidiReactor.
  virtual void OnSendInitialMetadataDone(bool ok) {}
  virtual void OnWriteDone(bool ok) {}
  void OnDone() override {}
  void OnCancel() override {}

 private:
  friend class ServerCallbackWriter<Response>;
  // May be overridden by internal implementation details. This is not a public
  // customization point.
  virtual void InternalBindWriter(ServerCallbackWriter<Response>* writer) {
    writer_ = writer;
  }

  ServerCallbackWriter<Response>* writer_;
};

}  // namespace experimental

namespace internal {

template <class Request, class Response>
class UnimplementedReadReactor
    : public experimental::ServerReadReactor<Request, Response> {
 public:
  void OnDone() override { delete this; }
  void OnStarted(::grpc_impl::ServerContext*, Response*) override {
    this->Finish(::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, ""));
  }
};

template <class Request, class Response>
class UnimplementedWriteReactor
    : public experimental::ServerWriteReactor<Request, Response> {
 public:
  void OnDone() override { delete this; }
  void OnStarted(::grpc_impl::ServerContext*, const Request*) override {
    this->Finish(::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, ""));
  }
};

template <class Request, class Response>
class UnimplementedBidiReactor
    : public experimental::ServerBidiReactor<Request, Response> {
 public:
  void OnDone() override { delete this; }
  void OnStarted(::grpc_impl::ServerContext*) override {
    this->Finish(::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, ""));
  }
};

template <class RequestType, class ResponseType>
class CallbackUnaryHandler : public grpc::internal::MethodHandler {
 public:
  CallbackUnaryHandler(
      std::function<void(::grpc_impl::ServerContext*, const RequestType*,
                         ResponseType*,
                         experimental::ServerCallbackRpcController*)>
          func)
      : func_(func) {}

  void SetMessageAllocator(
      ::grpc::experimental::MessageAllocator<RequestType, ResponseType>*
          allocator) {
    allocator_ = allocator;
  }

  void RunHandler(const HandlerParameter& param) final {
    // Arena allocate a controller structure (that includes request/response)
    ::grpc::g_core_codegen_interface->grpc_call_ref(param.call->call());
    auto* allocator_state = static_cast<
        grpc::experimental::MessageHolder<RequestType, ResponseType>*>(
        param.internal_data);
    auto* controller =
        new (::grpc::g_core_codegen_interface->grpc_call_arena_alloc(
            param.call->call(), sizeof(ServerCallbackRpcControllerImpl)))
            ServerCallbackRpcControllerImpl(param.server_context, param.call,
                                            allocator_state,
                                            std::move(param.call_requester));
    ::grpc::Status status = param.status;
    if (status.ok()) {
      // Call the actual function handler and expect the user to call finish
      grpc::internal::CatchingCallback(func_, param.server_context,
                                       controller->request(),
                                       controller->response(), controller);
    } else {
      // if deserialization failed, we need to fail the call
      controller->Finish(status);
    }
  }

  void* Deserialize(grpc_call* call, grpc_byte_buffer* req,
                    ::grpc::Status* status, void** handler_data) final {
    grpc::ByteBuffer buf;
    buf.set_buffer(req);
    RequestType* request = nullptr;
    ::grpc::experimental::MessageHolder<RequestType, ResponseType>*
        allocator_state = nullptr;
    if (allocator_ != nullptr) {
      allocator_state = allocator_->AllocateMessages();
    } else {
      allocator_state =
          new (::grpc::g_core_codegen_interface->grpc_call_arena_alloc(
              call, sizeof(DefaultMessageHolder<RequestType, ResponseType>)))
              DefaultMessageHolder<RequestType, ResponseType>();
    }
    *handler_data = allocator_state;
    request = allocator_state->request();
    *status =
        ::grpc::SerializationTraits<RequestType>::Deserialize(&buf, request);
    buf.Release();
    if (status->ok()) {
      return request;
    }
    // Clean up on deserialization failure.
    allocator_state->Release();
    return nullptr;
  }

 private:
  std::function<void(::grpc_impl::ServerContext*, const RequestType*,
                     ResponseType*, experimental::ServerCallbackRpcController*)>
      func_;
  grpc::experimental::MessageAllocator<RequestType, ResponseType>* allocator_ =
      nullptr;

  // The implementation class of ServerCallbackRpcController is a private member
  // of CallbackUnaryHandler since it is never exposed anywhere, and this allows
  // it to take advantage of CallbackUnaryHandler's friendships.
  class ServerCallbackRpcControllerImpl
      : public experimental::ServerCallbackRpcController {
   public:
    void Finish(::grpc::Status s) override {
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
                                     finish_ops_.SendMessagePtr(response()));
      } else {
        finish_ops_.ServerSendStatus(&ctx_->trailing_metadata_, s);
      }
      finish_ops_.set_core_cq_tag(&finish_tag_);
      call_.PerformOps(&finish_ops_);
    }

    void SendInitialMetadata(std::function<void(bool)> f) override {
      GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);
      callbacks_outstanding_.fetch_add(1, std::memory_order_relaxed);
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

    // Neither SetCancelCallback nor ClearCancelCallback should affect the
    // callbacks_outstanding_ count since they are paired and both must precede
    // the invocation of Finish (if they are used at all)
    void SetCancelCallback(std::function<void()> callback) override {
      ctx_->SetCancelCallback(std::move(callback));
    }

    void ClearCancelCallback() override { ctx_->ClearCancelCallback(); }

    grpc::experimental::RpcAllocatorState* GetRpcAllocatorState() override {
      return allocator_state_;
    }

   private:
    friend class CallbackUnaryHandler<RequestType, ResponseType>;

    ServerCallbackRpcControllerImpl(
        ServerContext* ctx, ::grpc::internal::Call* call,
        ::grpc::experimental::MessageHolder<RequestType, ResponseType>*
            allocator_state,
        std::function<void()> call_requester)
        : ctx_(ctx),
          call_(*call),
          allocator_state_(allocator_state),
          call_requester_(std::move(call_requester)) {
      ctx_->BeginCompletionOp(call, [this](bool) { MaybeDone(); }, nullptr);
    }

    const RequestType* request() { return allocator_state_->request(); }
    ResponseType* response() { return allocator_state_->response(); }

    void MaybeDone() {
      if (GPR_UNLIKELY(callbacks_outstanding_.fetch_sub(
                           1, std::memory_order_acq_rel) == 1)) {
        grpc_call* call = call_.call();
        auto call_requester = std::move(call_requester_);
        allocator_state_->Release();
        this->~ServerCallbackRpcControllerImpl();  // explicitly call destructor
        ::grpc::g_core_codegen_interface->grpc_call_unref(call);
        call_requester();
      }
    }

    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata>
        meta_ops_;
    grpc::internal::CallbackWithSuccessTag meta_tag_;
    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata,
                              grpc::internal::CallOpSendMessage,
                              grpc::internal::CallOpServerSendStatus>
        finish_ops_;
    grpc::internal::CallbackWithSuccessTag finish_tag_;

    ::grpc_impl::ServerContext* ctx_;
    grpc::internal::Call call_;
    grpc::experimental::MessageHolder<RequestType, ResponseType>* const
        allocator_state_;
    std::function<void()> call_requester_;
    std::atomic<intptr_t> callbacks_outstanding_{
        2};  // reserve for Finish and CompletionOp
  };
};

template <class RequestType, class ResponseType>
class CallbackClientStreamingHandler : public grpc::internal::MethodHandler {
 public:
  CallbackClientStreamingHandler(
      std::function<
          experimental::ServerReadReactor<RequestType, ResponseType>*()>
          func)
      : func_(std::move(func)) {}
  void RunHandler(const HandlerParameter& param) final {
    // Arena allocate a reader structure (that includes response)
    ::grpc::g_core_codegen_interface->grpc_call_ref(param.call->call());

    experimental::ServerReadReactor<RequestType, ResponseType>* reactor =
        param.status.ok()
            ? ::grpc::internal::CatchingReactorCreator<
                  experimental::ServerReadReactor<RequestType, ResponseType>>(
                  func_)
            : nullptr;

    if (reactor == nullptr) {
      // if deserialization or reactor creator failed, we need to fail the call
      reactor = new UnimplementedReadReactor<RequestType, ResponseType>;
    }

    auto* reader = new (::grpc::g_core_codegen_interface->grpc_call_arena_alloc(
        param.call->call(), sizeof(ServerCallbackReaderImpl)))
        ServerCallbackReaderImpl(param.server_context, param.call,
                                 std::move(param.call_requester), reactor);

    reader->BindReactor(reactor);
    reactor->OnStarted(param.server_context, reader->response());
    // The earliest that OnCancel can be called is after OnStarted is done.
    reactor->MaybeCallOnCancel();
    reader->MaybeDone();
  }

 private:
  std::function<experimental::ServerReadReactor<RequestType, ResponseType>*()>
      func_;

  class ServerCallbackReaderImpl
      : public experimental::ServerCallbackReader<RequestType> {
   public:
    void Finish(::grpc::Status s) override {
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
      callbacks_outstanding_.fetch_add(1, std::memory_order_relaxed);
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
      callbacks_outstanding_.fetch_add(1, std::memory_order_relaxed);
      read_ops_.RecvMessage(req);
      call_.PerformOps(&read_ops_);
    }

   private:
    friend class CallbackClientStreamingHandler<RequestType, ResponseType>;

    ServerCallbackReaderImpl(
        ::grpc_impl::ServerContext* ctx, grpc::internal::Call* call,
        std::function<void()> call_requester,
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
      if (GPR_UNLIKELY(callbacks_outstanding_.fetch_sub(
                           1, std::memory_order_acq_rel) == 1)) {
        reactor_->OnDone();
        grpc_call* call = call_.call();
        auto call_requester = std::move(call_requester_);
        this->~ServerCallbackReaderImpl();  // explicitly call destructor
        ::grpc::g_core_codegen_interface->grpc_call_unref(call);
        call_requester();
      }
    }

    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata>
        meta_ops_;
    grpc::internal::CallbackWithSuccessTag meta_tag_;
    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata,
                              grpc::internal::CallOpSendMessage,
                              grpc::internal::CallOpServerSendStatus>
        finish_ops_;
    grpc::internal::CallbackWithSuccessTag finish_tag_;
    grpc::internal::CallOpSet<grpc::internal::CallOpRecvMessage<RequestType>>
        read_ops_;
    grpc::internal::CallbackWithSuccessTag read_tag_;

    ::grpc_impl::ServerContext* ctx_;
    grpc::internal::Call call_;
    ResponseType resp_;
    std::function<void()> call_requester_;
    experimental::ServerReadReactor<RequestType, ResponseType>* reactor_;
    std::atomic<intptr_t> callbacks_outstanding_{
        3};  // reserve for OnStarted, Finish, and CompletionOp
  };
};

template <class RequestType, class ResponseType>
class CallbackServerStreamingHandler : public grpc::internal::MethodHandler {
 public:
  CallbackServerStreamingHandler(
      std::function<
          experimental::ServerWriteReactor<RequestType, ResponseType>*()>
          func)
      : func_(std::move(func)) {}
  void RunHandler(const HandlerParameter& param) final {
    // Arena allocate a writer structure
    ::grpc::g_core_codegen_interface->grpc_call_ref(param.call->call());

    experimental::ServerWriteReactor<RequestType, ResponseType>* reactor =
        param.status.ok()
            ? ::grpc::internal::CatchingReactorCreator<
                  experimental::ServerWriteReactor<RequestType, ResponseType>>(
                  func_)
            : nullptr;

    if (reactor == nullptr) {
      // if deserialization or reactor creator failed, we need to fail the call
      reactor = new UnimplementedWriteReactor<RequestType, ResponseType>;
    }

    auto* writer = new (::grpc::g_core_codegen_interface->grpc_call_arena_alloc(
        param.call->call(), sizeof(ServerCallbackWriterImpl)))
        ServerCallbackWriterImpl(param.server_context, param.call,
                                 static_cast<RequestType*>(param.request),
                                 std::move(param.call_requester), reactor);
    writer->BindReactor(reactor);
    reactor->OnStarted(param.server_context, writer->request());
    // The earliest that OnCancel can be called is after OnStarted is done.
    reactor->MaybeCallOnCancel();
    writer->MaybeDone();
  }

  void* Deserialize(grpc_call* call, grpc_byte_buffer* req,
                    ::grpc::Status* status, void** handler_data) final {
    ::grpc::ByteBuffer buf;
    buf.set_buffer(req);
    auto* request =
        new (::grpc::g_core_codegen_interface->grpc_call_arena_alloc(
            call, sizeof(RequestType))) RequestType();
    *status =
        ::grpc::SerializationTraits<RequestType>::Deserialize(&buf, request);
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
    void Finish(::grpc::Status s) override {
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
      callbacks_outstanding_.fetch_add(1, std::memory_order_relaxed);
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

    void Write(const ResponseType* resp,
               ::grpc::WriteOptions options) override {
      callbacks_outstanding_.fetch_add(1, std::memory_order_relaxed);
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

    void WriteAndFinish(const ResponseType* resp, ::grpc::WriteOptions options,
                        ::grpc::Status s) override {
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
        ::grpc_impl::ServerContext* ctx, grpc::internal::Call* call,
        const RequestType* req, std::function<void()> call_requester,
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
      if (GPR_UNLIKELY(callbacks_outstanding_.fetch_sub(
                           1, std::memory_order_acq_rel) == 1)) {
        reactor_->OnDone();
        grpc_call* call = call_.call();
        auto call_requester = std::move(call_requester_);
        this->~ServerCallbackWriterImpl();  // explicitly call destructor
        ::grpc::g_core_codegen_interface->grpc_call_unref(call);
        call_requester();
      }
    }

    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata>
        meta_ops_;
    grpc::internal::CallbackWithSuccessTag meta_tag_;
    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata,
                              grpc::internal::CallOpSendMessage,
                              grpc::internal::CallOpServerSendStatus>
        finish_ops_;
    grpc::internal::CallbackWithSuccessTag finish_tag_;
    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata,
                              grpc::internal::CallOpSendMessage>
        write_ops_;
    grpc::internal::CallbackWithSuccessTag write_tag_;

    ::grpc_impl::ServerContext* ctx_;
    grpc::internal::Call call_;
    const RequestType* req_;
    std::function<void()> call_requester_;
    experimental::ServerWriteReactor<RequestType, ResponseType>* reactor_;
    std::atomic<intptr_t> callbacks_outstanding_{
        3};  // reserve for OnStarted, Finish, and CompletionOp
  };
};

template <class RequestType, class ResponseType>
class CallbackBidiHandler : public grpc::internal::MethodHandler {
 public:
  CallbackBidiHandler(
      std::function<
          experimental::ServerBidiReactor<RequestType, ResponseType>*()>
          func)
      : func_(std::move(func)) {}
  void RunHandler(const HandlerParameter& param) final {
    ::grpc::g_core_codegen_interface->grpc_call_ref(param.call->call());

    experimental::ServerBidiReactor<RequestType, ResponseType>* reactor =
        param.status.ok()
            ? ::grpc::internal::CatchingReactorCreator<
                  experimental::ServerBidiReactor<RequestType, ResponseType>>(
                  func_)
            : nullptr;

    if (reactor == nullptr) {
      // if deserialization or reactor creator failed, we need to fail the call
      reactor = new UnimplementedBidiReactor<RequestType, ResponseType>;
    }

    auto* stream = new (::grpc::g_core_codegen_interface->grpc_call_arena_alloc(
        param.call->call(), sizeof(ServerCallbackReaderWriterImpl)))
        ServerCallbackReaderWriterImpl(param.server_context, param.call,
                                       std::move(param.call_requester),
                                       reactor);

    stream->BindReactor(reactor);
    reactor->OnStarted(param.server_context);
    // The earliest that OnCancel can be called is after OnStarted is done.
    reactor->MaybeCallOnCancel();
    stream->MaybeDone();
  }

 private:
  std::function<experimental::ServerBidiReactor<RequestType, ResponseType>*()>
      func_;

  class ServerCallbackReaderWriterImpl
      : public experimental::ServerCallbackReaderWriter<RequestType,
                                                        ResponseType> {
   public:
    void Finish(::grpc::Status s) override {
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
      callbacks_outstanding_.fetch_add(1, std::memory_order_relaxed);
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

    void Write(const ResponseType* resp,
               ::grpc::WriteOptions options) override {
      callbacks_outstanding_.fetch_add(1, std::memory_order_relaxed);
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

    void WriteAndFinish(const ResponseType* resp, ::grpc::WriteOptions options,
                        ::grpc::Status s) override {
      // Don't send any message if the status is bad
      if (s.ok()) {
        // TODO(vjpai): don't assert
        GPR_CODEGEN_ASSERT(finish_ops_.SendMessagePtr(resp, options).ok());
      }
      Finish(std::move(s));
    }

    void Read(RequestType* req) override {
      callbacks_outstanding_.fetch_add(1, std::memory_order_relaxed);
      read_ops_.RecvMessage(req);
      call_.PerformOps(&read_ops_);
    }

   private:
    friend class CallbackBidiHandler<RequestType, ResponseType>;

    ServerCallbackReaderWriterImpl(
        ::grpc_impl::ServerContext* ctx, grpc::internal::Call* call,
        std::function<void()> call_requester,
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
      if (GPR_UNLIKELY(callbacks_outstanding_.fetch_sub(
                           1, std::memory_order_acq_rel) == 1)) {
        reactor_->OnDone();
        grpc_call* call = call_.call();
        auto call_requester = std::move(call_requester_);
        this->~ServerCallbackReaderWriterImpl();  // explicitly call destructor
        ::grpc::g_core_codegen_interface->grpc_call_unref(call);
        call_requester();
      }
    }

    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata>
        meta_ops_;
    grpc::internal::CallbackWithSuccessTag meta_tag_;
    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata,
                              grpc::internal::CallOpSendMessage,
                              grpc::internal::CallOpServerSendStatus>
        finish_ops_;
    grpc::internal::CallbackWithSuccessTag finish_tag_;
    grpc::internal::CallOpSet<grpc::internal::CallOpSendInitialMetadata,
                              grpc::internal::CallOpSendMessage>
        write_ops_;
    grpc::internal::CallbackWithSuccessTag write_tag_;
    grpc::internal::CallOpSet<grpc::internal::CallOpRecvMessage<RequestType>>
        read_ops_;
    grpc::internal::CallbackWithSuccessTag read_tag_;

    ::grpc_impl::ServerContext* ctx_;
    grpc::internal::Call call_;
    std::function<void()> call_requester_;
    experimental::ServerBidiReactor<RequestType, ResponseType>* reactor_;
    std::atomic<intptr_t> callbacks_outstanding_{
        3};  // reserve for OnStarted, Finish, and CompletionOp
  };
};

}  // namespace internal

}  // namespace grpc_impl

#endif  // GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_IMPL_H
