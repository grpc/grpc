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

#ifndef GRPCXX_IMPL_CODEGEN_IMPL_ASYNC_STREAM_H
#define GRPCXX_IMPL_CODEGEN_IMPL_ASYNC_STREAM_H

#include <grpc++/impl/codegen/call.h>
#include <grpc++/impl/codegen/channel_interface.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/server_context.h>
#include <grpc++/impl/codegen/service_type.h>
#include <grpc++/impl/codegen/status.h>

namespace grpc {

class CompletionQueue;

/// Common interface for all client side asynchronous streaming.
class ClientAsyncStreamingInterface {
 public:
  virtual ~ClientAsyncStreamingInterface() {}

  /// Request notification of the reading of the initial metadata. Completion
  /// will be notified by \a tag on the associated completion queue.
  ///
  /// \param[in] tag Tag identifying this request.
  virtual void ReadInitialMetadata(void* tag) = 0;

  /// Request notification completion.
  ///
  /// \param[out] status To be updated with the operation status.
  /// \param[in] tag Tag identifying this request.
  virtual void Finish(Status* status, void* tag) = 0;
};

/// An interface that yields a sequence of messages of type \a R.
template <class R>
class AsyncReaderInterface {
 public:
  virtual ~AsyncReaderInterface() {}

  /// Read a message of type \a R into \a msg. Completion will be notified by \a
  /// tag on the associated completion queue.
  ///
  /// \param[out] msg Where to eventually store the read message.
  /// \param[in] tag The tag identifying the operation.
  virtual void Read(R* msg, void* tag) = 0;
};

/// An interface that can be fed a sequence of messages of type \a W.
template <class W>
class AsyncWriterInterface {
 public:
  virtual ~AsyncWriterInterface() {}

  /// Request the writing of \a msg with identifying tag \a tag.
  ///
  /// Only one write may be outstanding at any given time. This means that
  /// after calling Write, one must wait to receive \a tag from the completion
  /// queue BEFORE calling Write again.
  ///
  /// \param[in] msg The message to be written.
  /// \param[in] tag The tag identifying the operation.
  virtual void Write(const W& msg, void* tag) = 0;
};

template <class R>
class ClientAsyncReaderInterface : public ClientAsyncStreamingInterface,
                                   public AsyncReaderInterface<R> {};

template <class R>
class ClientAsyncReader GRPC_FINAL : public ClientAsyncReaderInterface<R> {
 public:
  /// Create a stream and write the first request out.
  template <class W>
  ClientAsyncReader(ChannelInterface* channel, CompletionQueue* cq,
                    const RpcMethod& method, ClientContext* context,
                    const W& request, void* tag)
      : context_(context), call_(channel->CreateCall(method, context, cq)) {
    init_ops_.set_output_tag(tag);
    init_ops_.SendInitialMetadata(context->send_initial_metadata_);
    // TODO(ctiller): don't assert
    GPR_CODEGEN_ASSERT(init_ops_.SendMessage(request).ok());
    init_ops_.ClientSendClose();
    call_.PerformOps(&init_ops_);
  }

  void ReadInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_CODEGEN_ASSERT(!context_->initial_metadata_received_);

    meta_ops_.set_output_tag(tag);
    meta_ops_.RecvInitialMetadata(context_);
    call_.PerformOps(&meta_ops_);
  }

  void Read(R* msg, void* tag) GRPC_OVERRIDE {
    read_ops_.set_output_tag(tag);
    if (!context_->initial_metadata_received_) {
      read_ops_.RecvInitialMetadata(context_);
    }
    read_ops_.RecvMessage(msg);
    call_.PerformOps(&read_ops_);
  }

  void Finish(Status* status, void* tag) GRPC_OVERRIDE {
    finish_ops_.set_output_tag(tag);
    if (!context_->initial_metadata_received_) {
      finish_ops_.RecvInitialMetadata(context_);
    }
    finish_ops_.ClientRecvStatus(context_, status);
    call_.PerformOps(&finish_ops_);
  }

 private:
  ClientContext* context_;
  Call call_;
  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage, CallOpClientSendClose>
      init_ops_;
  CallOpSet<CallOpRecvInitialMetadata> meta_ops_;
  CallOpSet<CallOpRecvInitialMetadata, CallOpRecvMessage<R>> read_ops_;
  CallOpSet<CallOpRecvInitialMetadata, CallOpClientRecvStatus> finish_ops_;
};

/// Common interface for client side asynchronous writing.
template <class W>
class ClientAsyncWriterInterface : public ClientAsyncStreamingInterface,
                                   public AsyncWriterInterface<W> {
 public:
  /// Signal the client is done with the writes.
  ///
  /// \param[in] tag The tag identifying the operation.
  virtual void WritesDone(void* tag) = 0;
};

template <class W>
class ClientAsyncWriter GRPC_FINAL : public ClientAsyncWriterInterface<W> {
 public:
  template <class R>
  ClientAsyncWriter(ChannelInterface* channel, CompletionQueue* cq,
                    const RpcMethod& method, ClientContext* context,
                    R* response, void* tag)
      : context_(context), call_(channel->CreateCall(method, context, cq)) {
    finish_ops_.RecvMessage(response);

    init_ops_.set_output_tag(tag);
    init_ops_.SendInitialMetadata(context->send_initial_metadata_);
    call_.PerformOps(&init_ops_);
  }

  void ReadInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_CODEGEN_ASSERT(!context_->initial_metadata_received_);

    meta_ops_.set_output_tag(tag);
    meta_ops_.RecvInitialMetadata(context_);
    call_.PerformOps(&meta_ops_);
  }

  void Write(const W& msg, void* tag) GRPC_OVERRIDE {
    write_ops_.set_output_tag(tag);
    // TODO(ctiller): don't assert
    GPR_CODEGEN_ASSERT(write_ops_.SendMessage(msg).ok());
    call_.PerformOps(&write_ops_);
  }

  void WritesDone(void* tag) GRPC_OVERRIDE {
    writes_done_ops_.set_output_tag(tag);
    writes_done_ops_.ClientSendClose();
    call_.PerformOps(&writes_done_ops_);
  }

  void Finish(Status* status, void* tag) GRPC_OVERRIDE {
    finish_ops_.set_output_tag(tag);
    if (!context_->initial_metadata_received_) {
      finish_ops_.RecvInitialMetadata(context_);
    }
    finish_ops_.ClientRecvStatus(context_, status);
    call_.PerformOps(&finish_ops_);
  }

 private:
  ClientContext* context_;
  Call call_;
  CallOpSet<CallOpSendInitialMetadata> init_ops_;
  CallOpSet<CallOpRecvInitialMetadata> meta_ops_;
  CallOpSet<CallOpSendMessage> write_ops_;
  CallOpSet<CallOpClientSendClose> writes_done_ops_;
  CallOpSet<CallOpRecvInitialMetadata, CallOpGenericRecvMessage,
            CallOpClientRecvStatus>
      finish_ops_;
};

/// Client-side interface for asynchronous bi-directional streaming.
template <class W, class R>
class ClientAsyncReaderWriterInterface : public ClientAsyncStreamingInterface,
                                         public AsyncWriterInterface<W>,
                                         public AsyncReaderInterface<R> {
 public:
  /// Signal the client is done with the writes.
  ///
  /// \param[in] tag The tag identifying the operation.
  virtual void WritesDone(void* tag) = 0;
};

template <class W, class R>
class ClientAsyncReaderWriter GRPC_FINAL
    : public ClientAsyncReaderWriterInterface<W, R> {
 public:
  ClientAsyncReaderWriter(ChannelInterface* channel, CompletionQueue* cq,
                          const RpcMethod& method, ClientContext* context,
                          void* tag)
      : context_(context), call_(channel->CreateCall(method, context, cq)) {
    init_ops_.set_output_tag(tag);
    init_ops_.SendInitialMetadata(context->send_initial_metadata_);
    call_.PerformOps(&init_ops_);
  }

  void ReadInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_CODEGEN_ASSERT(!context_->initial_metadata_received_);

    meta_ops_.set_output_tag(tag);
    meta_ops_.RecvInitialMetadata(context_);
    call_.PerformOps(&meta_ops_);
  }

  void Read(R* msg, void* tag) GRPC_OVERRIDE {
    read_ops_.set_output_tag(tag);
    if (!context_->initial_metadata_received_) {
      read_ops_.RecvInitialMetadata(context_);
    }
    read_ops_.RecvMessage(msg);
    call_.PerformOps(&read_ops_);
  }

  void Write(const W& msg, void* tag) GRPC_OVERRIDE {
    write_ops_.set_output_tag(tag);
    // TODO(ctiller): don't assert
    GPR_CODEGEN_ASSERT(write_ops_.SendMessage(msg).ok());
    call_.PerformOps(&write_ops_);
  }

  void WritesDone(void* tag) GRPC_OVERRIDE {
    writes_done_ops_.set_output_tag(tag);
    writes_done_ops_.ClientSendClose();
    call_.PerformOps(&writes_done_ops_);
  }

  void Finish(Status* status, void* tag) GRPC_OVERRIDE {
    finish_ops_.set_output_tag(tag);
    if (!context_->initial_metadata_received_) {
      finish_ops_.RecvInitialMetadata(context_);
    }
    finish_ops_.ClientRecvStatus(context_, status);
    call_.PerformOps(&finish_ops_);
  }

 private:
  ClientContext* context_;
  Call call_;
  CallOpSet<CallOpSendInitialMetadata> init_ops_;
  CallOpSet<CallOpRecvInitialMetadata> meta_ops_;
  CallOpSet<CallOpRecvInitialMetadata, CallOpRecvMessage<R>> read_ops_;
  CallOpSet<CallOpSendMessage> write_ops_;
  CallOpSet<CallOpClientSendClose> writes_done_ops_;
  CallOpSet<CallOpRecvInitialMetadata, CallOpClientRecvStatus> finish_ops_;
};

template <class W, class R>
class ServerAsyncReaderInterface : public ServerAsyncStreamingInterface,
                                   public AsyncReaderInterface<R> {
 public:
  virtual void Finish(const W& msg, const Status& status, void* tag) = 0;

  virtual void FinishWithError(const Status& status, void* tag) = 0;
};

template <class W, class R>
class ServerAsyncReader GRPC_FINAL : public ServerAsyncReaderInterface<W, R> {
 public:
  explicit ServerAsyncReader(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

  void SendInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);

    meta_ops_.set_output_tag(tag);
    meta_ops_.SendInitialMetadata(ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_.PerformOps(&meta_ops_);
  }

  void Read(R* msg, void* tag) GRPC_OVERRIDE {
    read_ops_.set_output_tag(tag);
    read_ops_.RecvMessage(msg);
    call_.PerformOps(&read_ops_);
  }

  void Finish(const W& msg, const Status& status, void* tag) GRPC_OVERRIDE {
    finish_ops_.set_output_tag(tag);
    if (!ctx_->sent_initial_metadata_) {
      finish_ops_.SendInitialMetadata(ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    // The response is dropped if the status is not OK.
    if (status.ok()) {
      finish_ops_.ServerSendStatus(ctx_->trailing_metadata_,
                                   finish_ops_.SendMessage(msg));
    } else {
      finish_ops_.ServerSendStatus(ctx_->trailing_metadata_, status);
    }
    call_.PerformOps(&finish_ops_);
  }

  void FinishWithError(const Status& status, void* tag) GRPC_OVERRIDE {
    GPR_CODEGEN_ASSERT(!status.ok());
    finish_ops_.set_output_tag(tag);
    if (!ctx_->sent_initial_metadata_) {
      finish_ops_.SendInitialMetadata(ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    finish_ops_.ServerSendStatus(ctx_->trailing_metadata_, status);
    call_.PerformOps(&finish_ops_);
  }

 private:
  void BindCall(Call* call) GRPC_OVERRIDE { call_ = *call; }

  Call call_;
  ServerContext* ctx_;
  CallOpSet<CallOpSendInitialMetadata> meta_ops_;
  CallOpSet<CallOpRecvMessage<R>> read_ops_;
  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
            CallOpServerSendStatus>
      finish_ops_;
};

template <class W>
class ServerAsyncWriterInterface : public ServerAsyncStreamingInterface,
                                   public AsyncWriterInterface<W> {
 public:
  virtual void Finish(const Status& status, void* tag) = 0;
};

template <class W>
class ServerAsyncWriter GRPC_FINAL : public ServerAsyncWriterInterface<W> {
 public:
  explicit ServerAsyncWriter(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

  void SendInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);

    meta_ops_.set_output_tag(tag);
    meta_ops_.SendInitialMetadata(ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_.PerformOps(&meta_ops_);
  }

  void Write(const W& msg, void* tag) GRPC_OVERRIDE {
    write_ops_.set_output_tag(tag);
    if (!ctx_->sent_initial_metadata_) {
      write_ops_.SendInitialMetadata(ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    // TODO(ctiller): don't assert
    GPR_CODEGEN_ASSERT(write_ops_.SendMessage(msg).ok());
    call_.PerformOps(&write_ops_);
  }

  void Finish(const Status& status, void* tag) GRPC_OVERRIDE {
    finish_ops_.set_output_tag(tag);
    if (!ctx_->sent_initial_metadata_) {
      finish_ops_.SendInitialMetadata(ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    finish_ops_.ServerSendStatus(ctx_->trailing_metadata_, status);
    call_.PerformOps(&finish_ops_);
  }

 private:
  void BindCall(Call* call) GRPC_OVERRIDE { call_ = *call; }

  Call call_;
  ServerContext* ctx_;
  CallOpSet<CallOpSendInitialMetadata> meta_ops_;
  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage> write_ops_;
  CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> finish_ops_;
};

/// Server-side interface for asynchronous bi-directional streaming.
template <class W, class R>
class ServerAsyncReaderWriterInterface : public ServerAsyncStreamingInterface,
                                         public AsyncWriterInterface<W>,
                                         public AsyncReaderInterface<R> {
 public:
  virtual void Finish(const Status& status, void* tag) = 0;
};

template <class W, class R>
class ServerAsyncReaderWriter GRPC_FINAL
    : public ServerAsyncReaderWriterInterface<W, R> {
 public:
  explicit ServerAsyncReaderWriter(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

  void SendInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);

    meta_ops_.set_output_tag(tag);
    meta_ops_.SendInitialMetadata(ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_.PerformOps(&meta_ops_);
  }

  void Read(R* msg, void* tag) GRPC_OVERRIDE {
    read_ops_.set_output_tag(tag);
    read_ops_.RecvMessage(msg);
    call_.PerformOps(&read_ops_);
  }

  void Write(const W& msg, void* tag) GRPC_OVERRIDE {
    write_ops_.set_output_tag(tag);
    if (!ctx_->sent_initial_metadata_) {
      write_ops_.SendInitialMetadata(ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    // TODO(ctiller): don't assert
    GPR_CODEGEN_ASSERT(write_ops_.SendMessage(msg).ok());
    call_.PerformOps(&write_ops_);
  }

  void Finish(const Status& status, void* tag) GRPC_OVERRIDE {
    finish_ops_.set_output_tag(tag);
    if (!ctx_->sent_initial_metadata_) {
      finish_ops_.SendInitialMetadata(ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    finish_ops_.ServerSendStatus(ctx_->trailing_metadata_, status);
    call_.PerformOps(&finish_ops_);
  }

 private:
  friend class ::grpc::Server;

  void BindCall(Call* call) GRPC_OVERRIDE { call_ = *call; }

  Call call_;
  ServerContext* ctx_;
  CallOpSet<CallOpSendInitialMetadata> meta_ops_;
  CallOpSet<CallOpRecvMessage<R>> read_ops_;
  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage> write_ops_;
  CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> finish_ops_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_IMPL_ASYNC_STREAM_H
