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

#include <grpc++/impl/codegen/call.h>
#include <grpc++/impl/codegen/channel_interface.h>
#include <grpc++/impl/codegen/client_context.h>
#include <grpc++/impl/codegen/server_context.h>
#include <grpc++/impl/codegen/service_type.h>
#include <grpc++/impl/codegen/status.h>
#include <grpc/impl/codegen/log.h>

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
class ClientAsyncResponseReader GRPC_FINAL
    : public ClientAsyncResponseReaderInterface<R> {
 public:
  template <class W>
  ClientAsyncResponseReader(ChannelInterface* channel, CompletionQueue* cq,
                            const RpcMethod& method, ClientContext* context,
                            const W& request)
      : context_(context),
        call_(channel->CreateCall(method, context, cq)),
        collection_(new CallOpSetCollection) {
    collection_->init_buf_.SetCollection(collection_);
    collection_->init_buf_.SendInitialMetadata(
        context->send_initial_metadata_, context->initial_metadata_flags());
    // TODO(ctiller): don't assert
    GPR_CODEGEN_ASSERT(collection_->init_buf_.SendMessage(request).ok());
    collection_->init_buf_.ClientSendClose();
    call_.PerformOps(&collection_->init_buf_);
  }

  void ReadInitialMetadata(void* tag) {
    GPR_CODEGEN_ASSERT(!context_->initial_metadata_received_);

    collection_->meta_buf_.SetCollection(collection_);
    collection_->meta_buf_.set_output_tag(tag);
    collection_->meta_buf_.RecvInitialMetadata(context_);
    call_.PerformOps(&collection_->meta_buf_);
  }

  void Finish(R* msg, Status* status, void* tag) {
    collection_->finish_buf_.SetCollection(collection_);
    collection_->finish_buf_.set_output_tag(tag);
    if (!context_->initial_metadata_received_) {
      collection_->finish_buf_.RecvInitialMetadata(context_);
    }
    collection_->finish_buf_.RecvMessage(msg);
    collection_->finish_buf_.AllowNoMessage();
    collection_->finish_buf_.ClientRecvStatus(context_, status);
    call_.PerformOps(&collection_->finish_buf_);
  }

 private:
  ClientContext* context_;
  Call call_;

  class CallOpSetCollection : public CallOpSetCollectionInterface {
   public:
    SneakyCallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
                    CallOpClientSendClose>
        init_buf_;
    CallOpSet<CallOpRecvInitialMetadata> meta_buf_;
    CallOpSet<CallOpRecvInitialMetadata, CallOpRecvMessage<R>,
              CallOpClientRecvStatus>
        finish_buf_;
  };
  std::shared_ptr<CallOpSetCollection> collection_;
};

template <class W>
class ServerAsyncResponseWriter GRPC_FINAL
    : public ServerAsyncStreamingInterface {
 public:
  explicit ServerAsyncResponseWriter(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

  void SendInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);

    meta_buf_.set_output_tag(tag);
    meta_buf_.SendInitialMetadata(ctx_->initial_metadata_,
                                  ctx_->initial_metadata_flags());
    ctx_->sent_initial_metadata_ = true;
    call_.PerformOps(&meta_buf_);
  }

  void Finish(const W& msg, const Status& status, void* tag) {
    finish_buf_.set_output_tag(tag);
    if (!ctx_->sent_initial_metadata_) {
      finish_buf_.SendInitialMetadata(ctx_->initial_metadata_,
                                      ctx_->initial_metadata_flags());
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
      ctx_->sent_initial_metadata_ = true;
    }
    finish_buf_.ServerSendStatus(ctx_->trailing_metadata_, status);
    call_.PerformOps(&finish_buf_);
  }

 private:
  void BindCall(Call* call) GRPC_OVERRIDE { call_ = *call; }

  Call call_;
  ServerContext* ctx_;
  CallOpSet<CallOpSendInitialMetadata> meta_buf_;
  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
            CallOpServerSendStatus>
      finish_buf_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_ASYNC_UNARY_CALL_H
