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
  virtual bool Write(const W& msg) = 0;
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
  ClientReader(ChannelInterface* channel, const RpcMethod& method,
               ClientContext* context, const grpc::protobuf::Message& request)
      : context_(context), call_(channel->CreateCall(method, context, &cq_)) {
    CallOpBuffer buf;
    buf.AddSendInitialMetadata(&context->send_initial_metadata_);
    buf.AddSendMessage(request);
    buf.AddClientSendClose();
    call_.PerformOps(&buf);
    cq_.Pluck(&buf);
  }

  // Blocking wait for initial metadata from server. The received metadata
  // can only be accessed after this call returns. Should only be called before
  // the first read. Calling this method is optional, and if it is not called
  // the metadata will be available in ClientContext after the first read.
  void WaitForInitialMetadata() {
    GPR_ASSERT(!context_->initial_metadata_received_);

    CallOpBuffer buf;
    buf.AddRecvInitialMetadata(context_);
    call_.PerformOps(&buf);
    cq_.Pluck(&buf);  // status ignored
  }

  bool Read(R* msg) GRPC_OVERRIDE {
    CallOpBuffer buf;
    if (!context_->initial_metadata_received_) {
      buf.AddRecvInitialMetadata(context_);
    }
    buf.AddRecvMessage(msg);
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf) && buf.got_message;
  }

  Status Finish() GRPC_OVERRIDE {
    CallOpBuffer buf;
    Status status;
    buf.AddClientRecvStatus(context_, &status);
    call_.PerformOps(&buf);
    GPR_ASSERT(cq_.Pluck(&buf));
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
class ClientWriter GRPC_FINAL : public ClientWriterInterface<W> {
 public:
  // Blocking create a stream.
  ClientWriter(ChannelInterface* channel, const RpcMethod& method,
               ClientContext* context, grpc::protobuf::Message* response)
      : context_(context),
        response_(response),
        call_(channel->CreateCall(method, context, &cq_)) {
    CallOpBuffer buf;
    buf.AddSendInitialMetadata(&context->send_initial_metadata_);
    call_.PerformOps(&buf);
    cq_.Pluck(&buf);
  }

  bool Write(const W& msg) GRPC_OVERRIDE {
    CallOpBuffer buf;
    buf.AddSendMessage(msg);
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf);
  }

  bool WritesDone() GRPC_OVERRIDE {
    CallOpBuffer buf;
    buf.AddClientSendClose();
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf);
  }

  // Read the final response and wait for the final status.
  Status Finish() GRPC_OVERRIDE {
    CallOpBuffer buf;
    Status status;
    buf.AddRecvMessage(response_);
    buf.AddClientRecvStatus(context_, &status);
    call_.PerformOps(&buf);
    GPR_ASSERT(cq_.Pluck(&buf));
    return status;
  }

 private:
  ClientContext* context_;
  grpc::protobuf::Message* const response_;
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
    CallOpBuffer buf;
    buf.AddSendInitialMetadata(&context->send_initial_metadata_);
    call_.PerformOps(&buf);
    cq_.Pluck(&buf);
  }

  // Blocking wait for initial metadata from server. The received metadata
  // can only be accessed after this call returns. Should only be called before
  // the first read. Calling this method is optional, and if it is not called
  // the metadata will be available in ClientContext after the first read.
  void WaitForInitialMetadata() {
    GPR_ASSERT(!context_->initial_metadata_received_);

    CallOpBuffer buf;
    buf.AddRecvInitialMetadata(context_);
    call_.PerformOps(&buf);
    cq_.Pluck(&buf);  // status ignored
  }

  bool Read(R* msg) GRPC_OVERRIDE {
    CallOpBuffer buf;
    if (!context_->initial_metadata_received_) {
      buf.AddRecvInitialMetadata(context_);
    }
    buf.AddRecvMessage(msg);
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf) && buf.got_message;
  }

  bool Write(const W& msg) GRPC_OVERRIDE {
    CallOpBuffer buf;
    buf.AddSendMessage(msg);
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf);
  }

  bool WritesDone() GRPC_OVERRIDE {
    CallOpBuffer buf;
    buf.AddClientSendClose();
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf);
  }

  Status Finish() GRPC_OVERRIDE {
    CallOpBuffer buf;
    Status status;
    buf.AddClientRecvStatus(context_, &status);
    call_.PerformOps(&buf);
    GPR_ASSERT(cq_.Pluck(&buf));
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

    CallOpBuffer buf;
    buf.AddSendInitialMetadata(&ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_->PerformOps(&buf);
    call_->cq()->Pluck(&buf);
  }

  bool Read(R* msg) GRPC_OVERRIDE {
    CallOpBuffer buf;
    buf.AddRecvMessage(msg);
    call_->PerformOps(&buf);
    return call_->cq()->Pluck(&buf) && buf.got_message;
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

    CallOpBuffer buf;
    buf.AddSendInitialMetadata(&ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_->PerformOps(&buf);
    call_->cq()->Pluck(&buf);
  }

  bool Write(const W& msg) GRPC_OVERRIDE {
    CallOpBuffer buf;
    if (!ctx_->sent_initial_metadata_) {
      buf.AddSendInitialMetadata(&ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    buf.AddSendMessage(msg);
    call_->PerformOps(&buf);
    return call_->cq()->Pluck(&buf);
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

    CallOpBuffer buf;
    buf.AddSendInitialMetadata(&ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_->PerformOps(&buf);
    call_->cq()->Pluck(&buf);
  }

  bool Read(R* msg) GRPC_OVERRIDE {
    CallOpBuffer buf;
    buf.AddRecvMessage(msg);
    call_->PerformOps(&buf);
    return call_->cq()->Pluck(&buf) && buf.got_message;
  }

  bool Write(const W& msg) GRPC_OVERRIDE {
    CallOpBuffer buf;
    if (!ctx_->sent_initial_metadata_) {
      buf.AddSendInitialMetadata(&ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    buf.AddSendMessage(msg);
    call_->PerformOps(&buf);
    return call_->cq()->Pluck(&buf);
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
                                   public AsyncReaderInterface<R> {
};

template <class R>
class ClientAsyncReader GRPC_FINAL : public ClientAsyncReaderInterface<R> {
 public:
  // Create a stream and write the first request out.
  ClientAsyncReader(ChannelInterface* channel, CompletionQueue* cq,
                    const RpcMethod& method, ClientContext* context,
                    const grpc::protobuf::Message& request, void* tag)
      : context_(context), call_(channel->CreateCall(method, context, cq)) {
    init_buf_.Reset(tag);
    init_buf_.AddSendInitialMetadata(&context->send_initial_metadata_);
    init_buf_.AddSendMessage(request);
    init_buf_.AddClientSendClose();
    call_.PerformOps(&init_buf_);
  }

  void ReadInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_ASSERT(!context_->initial_metadata_received_);

    meta_buf_.Reset(tag);
    meta_buf_.AddRecvInitialMetadata(context_);
    call_.PerformOps(&meta_buf_);
  }

  void Read(R* msg, void* tag) GRPC_OVERRIDE {
    read_buf_.Reset(tag);
    if (!context_->initial_metadata_received_) {
      read_buf_.AddRecvInitialMetadata(context_);
    }
    read_buf_.AddRecvMessage(msg);
    call_.PerformOps(&read_buf_);
  }

  void Finish(Status* status, void* tag) GRPC_OVERRIDE {
    finish_buf_.Reset(tag);
    if (!context_->initial_metadata_received_) {
      finish_buf_.AddRecvInitialMetadata(context_);
    }
    finish_buf_.AddClientRecvStatus(context_, status);
    call_.PerformOps(&finish_buf_);
  }

 private:
  ClientContext* context_;
  Call call_;
  CallOpBuffer init_buf_;
  CallOpBuffer meta_buf_;
  CallOpBuffer read_buf_;
  CallOpBuffer finish_buf_;
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
  ClientAsyncWriter(ChannelInterface* channel, CompletionQueue* cq,
                    const RpcMethod& method, ClientContext* context,
                    grpc::protobuf::Message* response, void* tag)
      : context_(context),
        response_(response),
        call_(channel->CreateCall(method, context, cq)) {
    init_buf_.Reset(tag);
    init_buf_.AddSendInitialMetadata(&context->send_initial_metadata_);
    call_.PerformOps(&init_buf_);
  }

  void ReadInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_ASSERT(!context_->initial_metadata_received_);

    meta_buf_.Reset(tag);
    meta_buf_.AddRecvInitialMetadata(context_);
    call_.PerformOps(&meta_buf_);
  }

  void Write(const W& msg, void* tag) GRPC_OVERRIDE {
    write_buf_.Reset(tag);
    write_buf_.AddSendMessage(msg);
    call_.PerformOps(&write_buf_);
  }

  void WritesDone(void* tag) GRPC_OVERRIDE {
    writes_done_buf_.Reset(tag);
    writes_done_buf_.AddClientSendClose();
    call_.PerformOps(&writes_done_buf_);
  }

  void Finish(Status* status, void* tag) GRPC_OVERRIDE {
    finish_buf_.Reset(tag);
    if (!context_->initial_metadata_received_) {
      finish_buf_.AddRecvInitialMetadata(context_);
    }
    finish_buf_.AddRecvMessage(response_);
    finish_buf_.AddClientRecvStatus(context_, status);
    call_.PerformOps(&finish_buf_);
  }

 private:
  ClientContext* context_;
  grpc::protobuf::Message* const response_;
  Call call_;
  CallOpBuffer init_buf_;
  CallOpBuffer meta_buf_;
  CallOpBuffer write_buf_;
  CallOpBuffer writes_done_buf_;
  CallOpBuffer finish_buf_;
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
    init_buf_.Reset(tag);
    init_buf_.AddSendInitialMetadata(&context->send_initial_metadata_);
    call_.PerformOps(&init_buf_);
  }

  void ReadInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_ASSERT(!context_->initial_metadata_received_);

    meta_buf_.Reset(tag);
    meta_buf_.AddRecvInitialMetadata(context_);
    call_.PerformOps(&meta_buf_);
  }

  void Read(R* msg, void* tag) GRPC_OVERRIDE {
    read_buf_.Reset(tag);
    if (!context_->initial_metadata_received_) {
      read_buf_.AddRecvInitialMetadata(context_);
    }
    read_buf_.AddRecvMessage(msg);
    call_.PerformOps(&read_buf_);
  }

  void Write(const W& msg, void* tag) GRPC_OVERRIDE {
    write_buf_.Reset(tag);
    write_buf_.AddSendMessage(msg);
    call_.PerformOps(&write_buf_);
  }

  void WritesDone(void* tag) GRPC_OVERRIDE {
    writes_done_buf_.Reset(tag);
    writes_done_buf_.AddClientSendClose();
    call_.PerformOps(&writes_done_buf_);
  }

  void Finish(Status* status, void* tag) GRPC_OVERRIDE {
    finish_buf_.Reset(tag);
    if (!context_->initial_metadata_received_) {
      finish_buf_.AddRecvInitialMetadata(context_);
    }
    finish_buf_.AddClientRecvStatus(context_, status);
    call_.PerformOps(&finish_buf_);
  }

 private:
  ClientContext* context_;
  Call call_;
  CallOpBuffer init_buf_;
  CallOpBuffer meta_buf_;
  CallOpBuffer read_buf_;
  CallOpBuffer write_buf_;
  CallOpBuffer writes_done_buf_;
  CallOpBuffer finish_buf_;
};

template <class W, class R>
class ServerAsyncReader GRPC_FINAL : public ServerAsyncStreamingInterface,
                                     public AsyncReaderInterface<R> {
 public:
  explicit ServerAsyncReader(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

  void SendInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_ASSERT(!ctx_->sent_initial_metadata_);

    meta_buf_.Reset(tag);
    meta_buf_.AddSendInitialMetadata(&ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_.PerformOps(&meta_buf_);
  }

  void Read(R* msg, void* tag) GRPC_OVERRIDE {
    read_buf_.Reset(tag);
    read_buf_.AddRecvMessage(msg);
    call_.PerformOps(&read_buf_);
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
  CallOpBuffer read_buf_;
  CallOpBuffer finish_buf_;
};

template <class W>
class ServerAsyncWriter GRPC_FINAL : public ServerAsyncStreamingInterface,
                                     public AsyncWriterInterface<W> {
 public:
  explicit ServerAsyncWriter(ServerContext* ctx)
      : call_(nullptr, nullptr, nullptr), ctx_(ctx) {}

  void SendInitialMetadata(void* tag) GRPC_OVERRIDE {
    GPR_ASSERT(!ctx_->sent_initial_metadata_);

    meta_buf_.Reset(tag);
    meta_buf_.AddSendInitialMetadata(&ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_.PerformOps(&meta_buf_);
  }

  void Write(const W& msg, void* tag) GRPC_OVERRIDE {
    write_buf_.Reset(tag);
    if (!ctx_->sent_initial_metadata_) {
      write_buf_.AddSendInitialMetadata(&ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    write_buf_.AddSendMessage(msg);
    call_.PerformOps(&write_buf_);
  }

  void Finish(const Status& status, void* tag) {
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
  CallOpBuffer write_buf_;
  CallOpBuffer finish_buf_;
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

    meta_buf_.Reset(tag);
    meta_buf_.AddSendInitialMetadata(&ctx_->initial_metadata_);
    ctx_->sent_initial_metadata_ = true;
    call_.PerformOps(&meta_buf_);
  }

  void Read(R* msg, void* tag) GRPC_OVERRIDE {
    read_buf_.Reset(tag);
    read_buf_.AddRecvMessage(msg);
    call_.PerformOps(&read_buf_);
  }

  void Write(const W& msg, void* tag) GRPC_OVERRIDE {
    write_buf_.Reset(tag);
    if (!ctx_->sent_initial_metadata_) {
      write_buf_.AddSendInitialMetadata(&ctx_->initial_metadata_);
      ctx_->sent_initial_metadata_ = true;
    }
    write_buf_.AddSendMessage(msg);
    call_.PerformOps(&write_buf_);
  }

  void Finish(const Status& status, void* tag) {
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
  CallOpBuffer read_buf_;
  CallOpBuffer write_buf_;
  CallOpBuffer finish_buf_;
};

}  // namespace grpc

#endif  // GRPCXX_STREAM_H
