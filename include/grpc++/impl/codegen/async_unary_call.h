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

#ifndef GRPCXX_IMPL_CODEGEN_ASYNC_UNARY_CALL_H
#define GRPCXX_IMPL_CODEGEN_ASYNC_UNARY_CALL_H

#include <assert.h>
#include <grpc++/impl/codegen/call.h>
#include <grpc++/impl/codegen/channel_interface.h>
#include <grpc++/impl/codegen/client_context.h>
#include <grpc++/impl/codegen/server_context.h>
#include <grpc++/impl/codegen/service_type.h>
#include <grpc++/impl/codegen/status.h>

namespace grpc {

class CompletionQueue;
extern CoreCodegenInterface* g_core_codegen_interface;

/// An interface relevant for async client side unary RPCS (which send
/// one request message to a server and receive one response message).
template <class R>
class ClientAsyncResponseReaderInterface {
 public:
  virtual ~ClientAsyncResponseReaderInterface() {}

  /// Request notification of the reading of initial metadata. Completion
  /// will be notified by \a tag on the associated completion queue.
  /// This call is optional, but if it is used, it cannot be used concurrently
  /// with or after the \a Finish method.
  ///
  /// \param[in] tag Tag identifying this request.
  virtual void ReadInitialMetadata(void* tag) = 0;

  /// Request to receive the server's response \a msg and final \a status for
  /// the call, and to notify \a tag on this call's completion queue when
  /// finished.
  ///
  /// This function will return when either:
  /// - when the server's response message and status have been received.
  /// - when the server has returned a non-OK status (no message expected in
  ///   this case).
  /// - when the call failed for some reason and the library generated a
  ///   non-OK status.
  ///
  /// \param[in] tag Tag identifying this request.
  /// \param[out] status To be updated with the operation status.
  /// \param[out] msg To be filled in with the server's response message.
  virtual void Finish(R* msg, Status* status, void* tag) = 0;
};

/// Async API for client-side unary RPCs, where the message response
/// received from the server is of type \a R.
template <class R>
class ClientAsyncResponseReader final
    : public ClientAsyncResponseReaderInterface<R> {
 public:
  /// Start a call and write the request out.
  /// \a tag will be notified on \a cq when the call has been started (i.e.
  /// intitial metadata sent) and \a request has been written out.
  /// Note that \a context will be used to fill in custom initial metadata
  /// used to send to the server when starting the call.
  template <class W>
  static ClientAsyncResponseReader* Create(ChannelInterface* channel,
                                           CompletionQueue* cq,
                                           const RpcMethod& method,
                                           ClientContext* context,
                                           const W& request) {
    Call call = channel->CreateCall(method, context, cq);
    return new (g_core_codegen_interface->grpc_call_arena_alloc(
        call.call(), sizeof(ClientAsyncResponseReader)))
        ClientAsyncResponseReader(call, context, request);
  }

  /// TODO(vjpai): Delete the below constructor
  /// PLEASE DO NOT USE THIS CONSTRUCTOR IN NEW CODE
  /// This code is only present as a short-term workaround
  /// for users that bypassed the code-generator and directly
  /// created this struct rather than properly using a stub.
  /// This code will not remain a valid public constructor for long.
  template <class W>
  ClientAsyncResponseReader(ChannelInterface* channel, CompletionQueue* cq,
                            const RpcMethod& method, ClientContext* context,
                            const W& request)
      : context_(context),
        call_(channel->CreateCall(method, context, cq)),
        collection_(std::make_shared<Ops>()) {
    collection_->init_buf.SetCollection(collection_);
    collection_->init_buf.SendInitialMetadata(
        context->send_initial_metadata_, context->initial_metadata_flags());
    // TODO(ctiller): don't assert
    GPR_CODEGEN_ASSERT(collection_->init_buf.SendMessage(request).ok());
    collection_->init_buf.ClientSendClose();
    call_.PerformOps(&collection_->init_buf);
  }

  // always allocated against a call arena, no memory free required
  static void operator delete(void* ptr, std::size_t size) {
    assert(size == sizeof(ClientAsyncResponseReader));
  }

  /// See \a ClientAsyncResponseReaderInterface::ReadInitialMetadata for
  /// semantics.
  ///
  /// Side effect:
  ///   - the \a ClientContext associated with this call is updated with
  ///     possible initial and trailing metadata sent from the serve.
  void ReadInitialMetadata(void* tag) {
    GPR_CODEGEN_ASSERT(!context_->initial_metadata_received_);

    Ops* o = &ops_;

    // TODO(vjpai): Remove the collection_ specialization as soon
    // as the public constructor is deleted
    if (collection_) {
      o = collection_.get();
      collection_->meta_buf.SetCollection(collection_);
    }

    o->meta_buf.set_output_tag(tag);
    o->meta_buf.RecvInitialMetadata(context_);
    call_.PerformOps(&o->meta_buf);
  }

  /// See \a ClientAysncResponseReaderInterface::Finish for semantics.
  ///
  /// Side effect:
  ///   - the \a ClientContext associated with this call is updated with
  ///     possible initial and trailing metadata sent from the server.
  void Finish(R* msg, Status* status, void* tag) {
    Ops* o = &ops_;

    // TODO(vjpai): Remove the collection_ specialization as soon
    // as the public constructor is deleted
    if (collection_) {
      o = collection_.get();
      collection_->finish_buf.SetCollection(collection_);
    }

    o->finish_buf.set_output_tag(tag);
    if (!context_->initial_metadata_received_) {
      o->finish_buf.RecvInitialMetadata(context_);
    }
    o->finish_buf.RecvMessage(msg);
    o->finish_buf.AllowNoMessage();
    o->finish_buf.ClientRecvStatus(context_, status);
    call_.PerformOps(&o->finish_buf);
  }

 private:
  ClientContext* const context_;
  Call call_;

  template <class W>
  ClientAsyncResponseReader(Call call, ClientContext* context, const W& request)
      : context_(context), call_(call) {
    ops_.init_buf.SendInitialMetadata(context->send_initial_metadata_,
                                      context->initial_metadata_flags());
    // TODO(ctiller): don't assert
    GPR_CODEGEN_ASSERT(ops_.init_buf.SendMessage(request).ok());
    ops_.init_buf.ClientSendClose();
    call_.PerformOps(&ops_.init_buf);
  }

  // disable operator new
  static void* operator new(std::size_t size);
  static void* operator new(std::size_t size, void* p) { return p; }

  // TODO(vjpai): Remove the reference to CallOpSetCollectionInterface
  // as soon as the related workaround (public constructor) is deleted
  struct Ops : public CallOpSetCollectionInterface {
    SneakyCallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
                    CallOpClientSendClose>
        init_buf;
    CallOpSet<CallOpRecvInitialMetadata> meta_buf;
    CallOpSet<CallOpRecvInitialMetadata, CallOpRecvMessage<R>,
              CallOpClientRecvStatus>
        finish_buf;
  } ops_;

  // TODO(vjpai): Remove the collection_ as soon as the related workaround
  // (public constructor) is deleted
  std::shared_ptr<Ops> collection_;
};

/// Async server-side API for handling unary calls, where the single
/// response message sent to the client is of type \a W.
template <class W>
class ServerAsyncResponseWriter final : public ServerAsyncStreamingInterface {
 public:
  explicit ServerAsyncResponseWriter(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

  /// See \a ServerAsyncStreamingInterface::SendInitialMetadata for semantics.
  ///
  /// Side effect:
  ///   The initial metadata that will be sent to the client from this op will
  ///   be taken from the \a ServerContext associated with the call.
  ///
  /// \param[in] tag Tag identifying this request.
  void SendInitialMetadata(void* tag) override {
    GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);

    meta_buf_.set_output_tag(tag);
    meta_buf_.SendInitialMetadata(ctx_->initial_metadata_,
                                  ctx_->initial_metadata_flags());
    if (ctx_->compression_level_set()) {
      meta_buf_.set_compression_level(ctx_->compression_level());
    }
    ctx_->sent_initial_metadata_ = true;
    call_.PerformOps(&meta_buf_);
  }

  /// Indicate that the stream is to be finished and request notification
  /// when the server has sent the appropriate signals to the client to
  /// end the call. Should not be used concurrently with other operations.
  ///
  /// \param[in] tag Tag identifying this request.
  /// \param[in] status To be sent to the client as the result of the call.
  /// \param[in] msg Message to be sent to the client.
  ///
  /// Side effect:
  ///   - also sends initial metadata if not already sent (using the
  ///     \a ServerContext associated with this call).
  ///
  /// Note: if \a status has a non-OK code, then \a msg will not be sent,
  /// and the client will receive only the status with possible trailing
  /// metadata.
  void Finish(const W& msg, const Status& status, void* tag) {
    finish_buf_.set_output_tag(tag);
    if (!ctx_->sent_initial_metadata_) {
      finish_buf_.SendInitialMetadata(ctx_->initial_metadata_,
                                      ctx_->initial_metadata_flags());
      if (ctx_->compression_level_set()) {
        finish_buf_.set_compression_level(ctx_->compression_level());
      }
      ctx_->sent_initial_metadata_ = true;
    }
    // The response is dropped if the status is not OK.
    if (status.ok()) {
      finish_buf_.ServerSendStatus(ctx_->trailing_metadata_,
                                   finish_buf_.SendMessage(msg));
    } else {
      finish_buf_.ServerSendStatus(ctx_->trailing_metadata_, status);
    }
    call_.PerformOps(&finish_buf_);
  }

  /// Indicate that the stream is to be finished with a non-OK status,
  /// and request notification for when the server has finished sending the
  /// appropriate signals to the client to end the call.
  /// Should not be used concurrently with other operations.
  ///
  /// \param[in] tag Tag identifying this request.
  /// \param[in] status To be sent to the client as the result of the call.
  ///   - Note: \a status must have a non-OK code.
  ///
  /// Side effect:
  ///   - also sends initial metadata if not already sent (using the
  ///     \a ServerContext associated with this call).
  void FinishWithError(const Status& status, void* tag) {
    GPR_CODEGEN_ASSERT(!status.ok());
    finish_buf_.set_output_tag(tag);
    if (!ctx_->sent_initial_metadata_) {
      finish_buf_.SendInitialMetadata(ctx_->initial_metadata_,
                                      ctx_->initial_metadata_flags());
      if (ctx_->compression_level_set()) {
        finish_buf_.set_compression_level(ctx_->compression_level());
      }
      ctx_->sent_initial_metadata_ = true;
    }
    finish_buf_.ServerSendStatus(ctx_->trailing_metadata_, status);
    call_.PerformOps(&finish_buf_);
  }

 private:
  void BindCall(Call* call) override { call_ = *call; }

  Call call_;
  ServerContext* ctx_;
  CallOpSet<CallOpSendInitialMetadata> meta_buf_;
  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
            CallOpServerSendStatus>
      finish_buf_;
};

}  // namespace grpc

namespace std {
template <class R>
class default_delete<grpc::ClientAsyncResponseReader<R>> {
 public:
  void operator()(void* p) {}
};
template <class R>
class default_delete<grpc::ClientAsyncResponseReaderInterface<R>> {
 public:
  void operator()(void* p) {}
};
}

#endif  // GRPCXX_IMPL_CODEGEN_ASYNC_UNARY_CALL_H
