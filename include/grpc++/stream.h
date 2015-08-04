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

#ifndef GRPCXX_STREAM_H
#define GRPCXX_STREAM_H

#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/server_context.h>
#include <grpc++/impl/call.h>
#include <grpc++/impl/service_type.h>
#include <grpc++/status.h>
#include <grpc/support/log.h>

namespace grpc {

// Common interface for all client side streaming.
class ClientStreamingInterface {
 public:
  virtual ~ClientStreamingInterface() {}

  // Wait until the stream finishes, and return the final status. When the
  // client side declares it has no more message to send, either implicitly or
  // by calling WritesDone, it needs to make sure there is no more message to
  // be received from the server, either implicitly or by getting a false from
  // a Read(). Otherwise, this implicitly cancels the stream.
  virtual Status Finish() = 0;
};

// An interface that yields a sequence of R messages.
template <class R>
class ReaderInterface {
 public:
  virtual ~ReaderInterface() {}

  // Blocking read a message and parse to msg. Returns true on success.
  // The method returns false when there will be no more incoming messages,
  // either because the other side has called WritesDone or the stream has
  // failed (or been cancelled).
  virtual bool Read(R* msg) = 0;
};

// An interface that can be fed a sequence of W messages.
template <class W>
class WriterInterface {
 public:
  virtual ~WriterInterface() {}

  // Blocking write msg to the stream. Returns true on success.
  // Returns false when the stream has been closed.
  virtual bool Write(const W& msg, const WriteOptions& options) = 0;

  inline bool Write(const W& msg) {
    return Write(msg, WriteOptions());
  }
};

template <class R>
class ClientReaderInterface : public ClientStreamingInterface,
                              public ReaderInterface<R> {
 public:
  virtual void WaitForInitialMetadata() = 0;
};

template <class R>
class ClientReader GRPC_FINAL : public ClientReaderInterface<R> {
 public:
  // Blocking create a stream and write the first request out.
  template <class W>
  ClientReader(ChannelInterface* channel, const RpcMethod& method,
               ClientContext* context, const W& request)
      : context_(context), call_(channel->CreateCall(method, context, &cq_)) {
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpClientSendClose> ops;
    ops.SendInitialMetadata(context->send_initial_metadata_);
    // TODO(ctiller): don't assert
    GPR_ASSERT(ops.SendMessage(request).ok());
    ops.ClientSendClose();
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);
  }

  // Blocking wait for initial metadata from server. The received metadata
  // can only be accessed after this call returns. Should only be called before
  // the first read. Calling this method is optional, and if it is not called
  // the metadata will be available in ClientContext after the first read.
  void WaitForInitialMetadata() {
    GPR_ASSERT(!context_->initial_metadata_received_);

    CallOpSet<CallOpRecvInitialMetadata> ops;
    ops.RecvInitialMetadata(context_);
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);  // status ignored
  }

  bool Read(R* msg) GRPC_OVERRIDE {
    CallOpSet<CallOpRecvInitialMetadata, CallOpRecvMessage<R>> ops;
    if (!context_->initial_metadata_received_) {
      ops.RecvInitialMetadata(context_);
    }
    ops.RecvMessage(msg);
    call_.PerformOps(&ops);
    return cq_.Pluck(&ops) && ops.got_message;
  }

  Status Finish() GRPC_OVERRIDE {
    CallOpSet<CallOpClientRecvStatus> ops;
    Status status;
    ops.ClientRecvStatus(context_, &status);
    call_.PerformOps(&ops);
    GPR_ASSERT(cq_.Pluck(&ops));
    return status;
  }

 private:
  ClientContext* context_;
  CompletionQueue cq_;
  Call call_;
};

template <class W>
class ClientWriterInterface : public ClientStreamingInterface,
                              public WriterInterface<W> {
 public:
  virtual bool WritesDone() = 0;
};

template <class W>
class ClientWriter : public ClientWriterInterface<W> {
 public:
  // Blocking create a stream.
  template <class R>
  ClientWriter(ChannelInterface* channel, const RpcMethod& method,
               ClientContext* context, R* response)
      : context_(context), call_(channel->CreateCall(method, context, &cq_)) {
    finish_ops_.RecvMessage(response);

    CallOpSet<CallOpSendInitialMetadata> ops;
    ops.SendInitialMetadata(context->send_initial_metadata_);
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);
  }

  using WriterInterface<W>::Write;
  bool Write(const W& msg, const WriteOptions& options) GRPC_OVERRIDE {
    CallOpSet<CallOpSendMessage> ops;
    if (!ops.SendMessage(msg, options).ok()) {
      return false;
    }
    call_.PerformOps(&ops);
    return cq_.Pluck(&ops);
  }

  bool WritesDone() GRPC_OVERRIDE {
    CallOpSet<CallOpClientSendClose> ops;
    ops.ClientSendClose();
    call_.PerformOps(&ops);
    return cq_.Pluck(&ops);
  }

  // Read the final response and wait for the final status.
  Status Finish() GRPC_OVERRIDE {
    Status status;
    finish_ops_.ClientRecvStatus(context_, &status);
    call_.PerformOps(&finish_ops_);
    GPR_ASSERT(cq_.Pluck(&finish_ops_));
    return status;
  }

 private:
  ClientContext* context_;
  CallOpSet<CallOpGenericRecvMessage, CallOpClientRecvStatus> finish_ops_;
  CompletionQueue cq_;
  Call call_;
};

// Client-side interface for bi-directional streaming.
template <class W, class R>
class ClientReaderWriterInterface : public ClientStreamingInterface,
                                    public WriterInterface<W>,
                                    public ReaderInterface<R> {
 public:
  virtual void WaitForInitialMetadata() = 0;
  virtual bool WritesDone() = 0;
};

template <class W, class R>
class ClientReaderWriter GRPC_FINAL : public ClientReaderWriterInterface<W, R> {
 public:
  // Blocking create a stream.
  ClientReaderWriter(ChannelInterface* channel, const RpcMethod& method,
                     ClientContext* context)
      : context_(context), call_(channel->CreateCall(method, context, &cq_)) {
    CallOpSet<CallOpSendInitialMetadata> ops;
    ops.SendInitialMetadata(context->send_initial_metadata_);
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);
  }

  // Blocking wait for initial metadata from server. The received metadata
  // can only be accessed after this call returns. Should only be called before
  // the first read. Calling this method is optional, and if it is not called
  // the metadata will be available in ClientContext after the first read.
  void WaitForInitialMetadata() {
    GPR_ASSERT(!context_->initial_metadata_received_);

    CallOpSet<CallOpRecvInitialMetadata> ops;
    ops.RecvInitialMetadata(context_);
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);  // status ignored
  }

  bool Read(R* msg) GRPC_OVERRIDE {
    CallOpSet<CallOpRecvInitialMetadata, CallOpRecvMessage<R>> ops;
    if (!context_->initial_metadata_received_) {
      ops.RecvInitialMetadata(context_);
    }
    ops.RecvMessage(msg);
    call_.PerformOps(&ops);
    return cq_.Pluck(&ops) && ops.got_message;
  }

  using WriterInterface<W>::Write;
  bool Write(const W& msg, const WriteOptions& options) GRPC_OVERRIDE {
    CallOpSet<CallOpSendMessage> ops;
    if (!ops.SendMessage(msg, options).ok()) return false;
    call_.PerformOps(&ops);
    return cq_.Pluck(&ops);
  }

  bool WritesDone() GRPC_OVERRIDE {
    CallOpSet<CallOpClientSendClose> ops;
    ops.ClientSendClose();
    call_.PerformOps(&ops);
    return cq_.Pluck(&ops);
  }

  Status Finish() GRPC_OVERRIDE {
    CallOpSet<CallOpClientRecvStatus> ops;
    Status status;
    ops.ClientRecvStatus(context_, &status);
    call_.PerformOps(&ops);
    GPR_ASSERT(cq_.Pluck(&ops));
    return status;
  }

 private:
  ClientContext* context_;
  CompletionQueue cq_;
  Call call_;
};

template <class R>
class ServerReader GRPC_FINAL : public ReaderInterface<R> {
 public:
  ServerReader(Call* call, ServerContext* ctx) : call_(call), ctx_(ctx) {}

  void SendInitialMetadata() {
    GPR_ASSERT(!ctx_->sent_initial_metadata_);

    CallOpSet<CallOpSendInitialMetadata> ops;
    ops.SendInitialMetadata(ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_->PerformOps(&ops);
    call_->cq()->Pluck(&ops);
  }

  bool Read(R* msg) GRPC_OVERRIDE {
    CallOpSet<CallOpRecvMessage<R>> ops;
    ops.RecvMessage(msg);
    call_->PerformOps(&ops);
    return call_->cq()->Pluck(&ops) && ops.got_message;
  }

 private:
  Call* const call_;
  ServerContext* const ctx_;
};

template <class W>
class ServerWriter GRPC_FINAL : public WriterInterface<W> {
 public:
  ServerWriter(Call* call, ServerContext* ctx) : call_(call), ctx_(ctx) {}

  void SendInitialMetadata() {
    GPR_ASSERT(!ctx_->sent_initial_metadata_);

    CallOpSet<CallOpSendInitialMetadata> ops;
    ops.SendInitialMetadata(ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_->PerformOps(&ops);
    call_->cq()->Pluck(&ops);
  }

  using WriterInterface<W>::Write;
  bool Write(const W& msg, const WriteOptions& options) GRPC_OVERRIDE {
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage> ops;
    if (!ops.SendMessage(msg, options).ok()) {
      return false;
    }
    if (!ctx_->sent_initial_metadata_) {
      ops.SendInitialMetadata(ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    call_->PerformOps(&ops);
    return call_->cq()->Pluck(&ops);
  }

 private:
  Call* const call_;
  ServerContext* const ctx_;
};

// Server-side interface for bi-directional streaming.
template <class W, class R>
class ServerReaderWriter GRPC_FINAL : public WriterInterface<W>,
                                      public ReaderInterface<R> {
 public:
  ServerReaderWriter(Call* call, ServerContext* ctx) : call_(call), ctx_(ctx) {}

  void SendInitialMetadata() {
    GPR_ASSERT(!ctx_->sent_initial_metadata_);

    CallOpSet<CallOpSendInitialMetadata> ops;
    ops.SendInitialMetadata(ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_->PerformOps(&ops);
    call_->cq()->Pluck(&ops);
  }

  bool Read(R* msg) GRPC_OVERRIDE {
    CallOpSet<CallOpRecvMessage<R>> ops;
    ops.RecvMessage(msg);
    call_->PerformOps(&ops);
    return call_->cq()->Pluck(&ops) && ops.got_message;
  }

  using WriterInterface<W>::Write;
  bool Write(const W& msg, const WriteOptions& options) GRPC_OVERRIDE {
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage> ops;
    if (!ops.SendMessage(msg, options).ok()) {
      return false;
    }
    if (!ctx_->sent_initial_metadata_) {
      ops.SendInitialMetadata(ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    call_->PerformOps(&ops);
    return call_->cq()->Pluck(&ops);
  }

 private:
  Call* const call_;
  ServerContext* const ctx_;
};

// Async interfaces
// Common interface for all client side streaming.
class ClientAsyncStreamingInterface {
 public:
  virtual ~ClientAsyncStreamingInterface() {}

  virtual void ReadInitialMetadata(void* tag) = 0;

  virtual void Finish(Status* status, void* tag) = 0;
};

// An interface that yields a sequence of R messages.
template <class R>
class AsyncReaderInterface {
 public:
  virtual ~AsyncReaderInterface() {}

  virtual void Read(R* msg, void* tag) = 0;
};

// An interface that can be fed a sequence of W messages.
template <class W>
class AsyncWriterInterface {
 public:
  virtual ~AsyncWriterInterface() {}

  virtual void Write(const W& msg, void* tag) = 0;
};

template <class R>
class ClientAsyncReaderInterface : public ClientAsyncStreamingInterface,
                                   public AsyncReaderInterface<R> {};

template <class R>
class ClientAsyncReader GRPC_FINAL : public ClientAsyncReaderInterface<R> {
 public:
  // Create a stream and write the first request out.
  template <class W>
  ClientAsyncReader(ChannelInterface* channel, CompletionQueue* cq,
                    const RpcMethod& method, ClientContext* context,
                    const W& request, void* tag)
      : context_(context), call_(channel->CreateCall(method, context, cq)) {
    init_ops_.set_output_tag(tag);
    init_ops_.SendInitialMetadata(context->send_initial_metadata_);
    // TODO(ctiller): don't assert
    GPR_ASSERT(init_ops_.SendMessage(request).ok());
    init_ops_.ClientSendClose();
    call_.PerformOps(&init_ops_);
  }

  void ReadInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_ASSERT(!context_->initial_metadata_received_);

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

template <class W>
class ClientAsyncWriterInterface : public ClientAsyncStreamingInterface,
                                   public AsyncWriterInterface<W> {
 public:
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
    GPR_ASSERT(!context_->initial_metadata_received_);

    meta_ops_.set_output_tag(tag);
    meta_ops_.RecvInitialMetadata(context_);
    call_.PerformOps(&meta_ops_);
  }

  void Write(const W& msg, void* tag) GRPC_OVERRIDE {
    write_ops_.set_output_tag(tag);
    // TODO(ctiller): don't assert
    GPR_ASSERT(write_ops_.SendMessage(msg).ok());
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
            CallOpClientRecvStatus> finish_ops_;
};

// Client-side interface for bi-directional streaming.
template <class W, class R>
class ClientAsyncReaderWriterInterface : public ClientAsyncStreamingInterface,
                                         public AsyncWriterInterface<W>,
                                         public AsyncReaderInterface<R> {
 public:
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
    GPR_ASSERT(!context_->initial_metadata_received_);

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
    GPR_ASSERT(write_ops_.SendMessage(msg).ok());
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
class ServerAsyncReader GRPC_FINAL : public ServerAsyncStreamingInterface,
                                     public AsyncReaderInterface<R> {
 public:
  explicit ServerAsyncReader(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

  void SendInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_ASSERT(!ctx_->sent_initial_metadata_);

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

  void Finish(const W& msg, const Status& status, void* tag) {
    finish_ops_.set_output_tag(tag);
    if (!ctx_->sent_initial_metadata_) {
      finish_ops_.SendInitialMetadata(ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    // The response is dropped if the status is not OK.
    if (status.ok()) {
      finish_ops_.ServerSendStatus(
          ctx_->trailing_metadata_,
          finish_ops_.SendMessage(msg));
    } else {
      finish_ops_.ServerSendStatus(ctx_->trailing_metadata_, status);
    }
    call_.PerformOps(&finish_ops_);
  }

  void FinishWithError(const Status& status, void* tag) {
    GPR_ASSERT(!status.ok());
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
            CallOpServerSendStatus> finish_ops_;
};

template <class W>
class ServerAsyncWriter GRPC_FINAL : public ServerAsyncStreamingInterface,
                                     public AsyncWriterInterface<W> {
 public:
  explicit ServerAsyncWriter(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

  void SendInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_ASSERT(!ctx_->sent_initial_metadata_);

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
    GPR_ASSERT(write_ops_.SendMessage(msg).ok());
    call_.PerformOps(&write_ops_);
  }

  void Finish(const Status& status, void* tag) {
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

// Server-side interface for bi-directional streaming.
template <class W, class R>
class ServerAsyncReaderWriter GRPC_FINAL : public ServerAsyncStreamingInterface,
                                           public AsyncWriterInterface<W>,
                                           public AsyncReaderInterface<R> {
 public:
  explicit ServerAsyncReaderWriter(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

  void SendInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_ASSERT(!ctx_->sent_initial_metadata_);

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
    GPR_ASSERT(write_ops_.SendMessage(msg).ok());
    call_.PerformOps(&write_ops_);
  }

  void Finish(const Status& status, void* tag) {
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
  CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage> write_ops_;
  CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> finish_ops_;
};

}  // namespace grpc

#endif  // GRPCXX_STREAM_H
