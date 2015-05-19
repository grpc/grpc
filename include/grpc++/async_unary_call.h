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

#ifndef GRPCXX_ASYNC_UNARY_CALL_H
#define GRPCXX_ASYNC_UNARY_CALL_H

#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/server_context.h>
#include <grpc++/impl/call.h>
#include <grpc++/impl/service_type.h>
#include <grpc++/status.h>
#include <grpc/support/log.h>

namespace grpc {

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
  ClientAsyncResponseReader(ChannelInterface* channel, CompletionQueue* cq,
                            const RpcMethod& method, ClientContext* context,
                            const grpc::protobuf::Message& request)
      : context_(context), call_(channel->CreateCall(method, context, cq)) {
    init_buf_.AddSendInitialMetadata(&context->send_initial_metadata_);
    init_buf_.AddSendMessage(request);
    init_buf_.AddClientSendClose();
    call_.PerformOps(&init_buf_);
  }

  void ReadInitialMetadata(void* tag) {
    GPR_ASSERT(!context_->initial_metadata_received_);

    meta_buf_.Reset(tag);
    meta_buf_.AddRecvInitialMetadata(context_);
    call_.PerformOps(&meta_buf_);
  }

  void Finish(R* msg, Status* status, void* tag) {
    finish_buf_.Reset(tag);
    if (!context_->initial_metadata_received_) {
      finish_buf_.AddRecvInitialMetadata(context_);
    }
    finish_buf_.AddRecvMessage(msg);
    finish_buf_.AddClientRecvStatus(context_, status);
    call_.PerformOps(&finish_buf_);
  }

 private:
  ClientContext* context_;
  Call call_;
  SneakyCallOpBuffer init_buf_;
  CallOpBuffer meta_buf_;
  CallOpBuffer finish_buf_;
};

template <class W>
class ServerAsyncResponseWriter GRPC_FINAL
    : public ServerAsyncStreamingInterface {
 public:
  explicit ServerAsyncResponseWriter(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

  void SendInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_ASSERT(!ctx_->sent_initial_metadata_);

    meta_buf_.Reset(tag);
    meta_buf_.AddSendInitialMetadata(&ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_.PerformOps(&meta_buf_);
  }

  void Finish(const W& msg, const Status& status, void* tag) {
    finish_buf_.Reset(tag);
    if (!ctx_->sent_initial_metadata_) {
      finish_buf_.AddSendInitialMetadata(&ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    // The response is dropped if the status is not OK.
    if (status.IsOk()) {
      finish_buf_.AddSendMessage(msg);
    }
    finish_buf_.AddServerSendStatus(&ctx_->trailing_metadata_, status);
    call_.PerformOps(&finish_buf_);
  }

  void FinishWithError(const Status& status, void* tag) {
    GPR_ASSERT(!status.IsOk());
    finish_buf_.Reset(tag);
    if (!ctx_->sent_initial_metadata_) {
      finish_buf_.AddSendInitialMetadata(&ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    finish_buf_.AddServerSendStatus(&ctx_->trailing_metadata_, status);
    call_.PerformOps(&finish_buf_);
  }

 private:
  void BindCall(Call* call) GRPC_OVERRIDE { call_ = *call; }

  Call call_;
  ServerContext* ctx_;
  CallOpBuffer meta_buf_;
  CallOpBuffer finish_buf_;
};

}  // namespace grpc

#endif  // GRPCXX_ASYNC_UNARY_CALL_H
