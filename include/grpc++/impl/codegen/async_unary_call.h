/*
 *
 * Copyright 2015, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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
  ClientAsyncResponseReader(ChannelInterface* channel, CompletionQueue* cq,
                            const RpcMethod& method, ClientContext* context,
                            const W& request)
      : context_(context),
        call_(channel->CreateCall(method, context, cq)),
        collection_(std::make_shared<CallOpSetCollection>()) {
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

#endif  // GRPCXX_IMPL_CODEGEN_ASYNC_UNARY_CALL_H
