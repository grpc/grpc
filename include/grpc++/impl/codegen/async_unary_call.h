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

template <class R>
class ClientAsyncResponseReaderInterface {
 public:
  virtual ~ClientAsyncResponseReaderInterface() {}
  virtual void ReadInitialMetadata(void* tag) = 0;
  virtual void Finish(R* msg, Status* status, void* tag) = 0;
};

template <class R>
class ClientAsyncResponseReader final
    : public ClientAsyncResponseReaderInterface<R> {
 public:
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

  /// always allocated against a call arena, no memory free required
  static void operator delete(void* ptr, std::size_t size) {
    assert(size == sizeof(ClientAsyncResponseReader));
  }

  void ReadInitialMetadata(void* tag) {
    GPR_CODEGEN_ASSERT(!context_->initial_metadata_received_);

    meta_buf_.set_output_tag(tag);
    meta_buf_.RecvInitialMetadata(context_);
    call_.PerformOps(&meta_buf_);
  }

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

template <class W>
class ServerAsyncResponseWriter final : public ServerAsyncStreamingInterface {
 public:
  explicit ServerAsyncResponseWriter(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

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
