/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

    meta_buf_.set_output_tag(tag);
    meta_buf_.RecvInitialMetadata(context_);
    call_.PerformOps(&meta_buf_);
  }

  /// See \a ClientAysncResponseReaderInterface::Finish for semantics.
  ///
  /// Side effect:
  ///   - the \a ClientContext associated with this call is updated with
  ///     possible initial and trailing metadata sent from the server.
  void Finish(R* msg, Status* status, void* tag) {
    finish_buf_.set_output_tag(tag);
    if (!context_->initial_metadata_received_) {
      finish_buf_.RecvInitialMetadata(context_);
    }
    finish_buf_.RecvMessage(msg);
    finish_buf_.AllowNoMessage();
    finish_buf_.ClientRecvStatus(context_, status);
    call_.PerformOps(&finish_buf_);
  }

 private:
  ClientContext* const context_;
  Call call_;

  template <class W>
  ClientAsyncResponseReader(Call call, ClientContext* context, const W& request)
      : context_(context), call_(call) {
    init_buf_.SendInitialMetadata(context->send_initial_metadata_,
                                  context->initial_metadata_flags());
    // TODO(ctiller): don't assert
    GPR_CODEGEN_ASSERT(init_buf_.SendMessage(request).ok());
    init_buf_.ClientSendClose();
    call_.PerformOps(&init_buf_);
  }

  // disable operator new
  static void* operator new(std::size_t size);
  static void* operator new(std::size_t size, void* p) { return p; };

  SneakyCallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
                  CallOpClientSendClose>
      init_buf_;
  CallOpSet<CallOpRecvInitialMetadata> meta_buf_;
  CallOpSet<CallOpRecvInitialMetadata, CallOpRecvMessage<R>,
            CallOpClientRecvStatus>
      finish_buf_;
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
}

#endif  // GRPCXX_IMPL_CODEGEN_ASYNC_UNARY_CALL_H
