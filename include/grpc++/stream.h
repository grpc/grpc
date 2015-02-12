/*
 *
 * Copyright 2014, Google Inc.
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

#ifndef __GRPCPP_STREAM_H__
#define __GRPCPP_STREAM_H__

#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/server_context.h>
#include <grpc++/impl/call.h>
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
class ClientReader final : public ClientStreamingInterface,
                           public ReaderInterface<R> {
 public:
  // Blocking create a stream and write the first request out.
  ClientReader(ChannelInterface *channel, const RpcMethod &method,
               ClientContext *context,
               const google::protobuf::Message &request)
      : context_(context), call_(channel->CreateCall(method, context, &cq_)) {
    CallOpBuffer buf;
    buf.AddSendMessage(request);
    buf.AddClientSendClose();
    call_.PerformOps(&buf);
    cq_.Pluck(&buf);
  }

  virtual bool Read(R *msg) override {
    CallOpBuffer buf;
    bool got_message;
    buf.AddRecvMessage(msg, &got_message);
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf) && got_message;
  }

  virtual Status Finish() override {
    CallOpBuffer buf;
    Status status;
    buf.AddClientRecvStatus(&context_->trailing_metadata_, &status);
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
class ClientWriter final : public ClientStreamingInterface,
                           public WriterInterface<W> {
 public:
  // Blocking create a stream.
  ClientWriter(ChannelInterface *channel, const RpcMethod &method,
               ClientContext *context,
               google::protobuf::Message *response)
      : context_(context), response_(response),
        call_(channel->CreateCall(method, context, &cq_)) {
    CallOpBuffer buf;
    buf.AddSendInitialMetadata(&context->send_initial_metadata_);
    call_.PerformOps(&buf);
    cq_.Pluck(&buf);
  }

  virtual bool Write(const W& msg) override {
    CallOpBuffer buf;
    buf.AddSendMessage(msg);
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf);
  }

  virtual bool WritesDone() {
    CallOpBuffer buf;
    buf.AddClientSendClose();
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf);
  }

  // Read the final response and wait for the final status.
  virtual Status Finish() override {
    CallOpBuffer buf;
    Status status;
    bool got_message;
    buf.AddRecvMessage(response_, &got_message);
    buf.AddClientRecvStatus(&context_->trailing_metadata_, &status);
    call_.PerformOps(&buf);
    GPR_ASSERT(cq_.Pluck(&buf) && got_message);
    return status;
  }

 private:
  ClientContext* context_;
  google::protobuf::Message *const response_;
  CompletionQueue cq_;
  Call call_;
};

// Client-side interface for bi-directional streaming.
template <class W, class R>
class ClientReaderWriter final : public ClientStreamingInterface,
                                 public WriterInterface<W>,
                                 public ReaderInterface<R> {
 public:
  // Blocking create a stream.
  ClientReaderWriter(ChannelInterface *channel,
                     const RpcMethod &method, ClientContext *context)
      : context_(context), call_(channel->CreateCall(method, context, &cq_)) {}

  virtual bool Read(R *msg) override {
    CallOpBuffer buf;
    bool got_message;
    buf.AddRecvMessage(msg, &got_message);
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf) && got_message;
  }

  virtual bool Write(const W& msg) override {
    CallOpBuffer buf;
    buf.AddSendMessage(msg);
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf);
  }

  virtual bool WritesDone() {
    CallOpBuffer buf;
    buf.AddClientSendClose();
    call_.PerformOps(&buf);
    return cq_.Pluck(&buf);
  }

  virtual Status Finish() override {
    CallOpBuffer buf;
    Status status;
    buf.AddClientRecvStatus(&context_->trailing_metadata_, &status);
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
class ServerReader final : public ReaderInterface<R> {
 public:
  explicit ServerReader(Call* call, ServerContext* ctx) : call_(call), ctx_(ctx) {}

  virtual bool Read(R* msg) override {
    CallOpBuffer buf;
    bool got_message;
    buf.AddRecvMessage(msg, &got_message);
    call_->PerformOps(&buf);
    return call_->cq()->Pluck(&buf) && got_message;
  }

 private:
  Call* const call_;
  ServerContext* const ctx_;
};

template <class W>
class ServerWriter final : public WriterInterface<W> {
 public:
  explicit ServerWriter(Call* call, ServerContext* ctx) : call_(call), ctx_(ctx) {}

  virtual bool Write(const W& msg) override {
    CallOpBuffer buf;
    ctx_->SendInitialMetadataIfNeeded(&buf);
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
class ServerReaderWriter final : public WriterInterface<W>,
                           public ReaderInterface<R> {
 public:
  explicit ServerReaderWriter(Call* call, ServerContext* ctx) : call_(call), ctx_(ctx) {}

  virtual bool Read(R* msg) override {
    CallOpBuffer buf;
    bool got_message;
    buf.AddRecvMessage(msg, &got_message);
    call_->PerformOps(&buf);
    return call_->cq()->Pluck(&buf) && got_message;
  }

  virtual bool Write(const W& msg) override {
    CallOpBuffer buf;
    ctx_->SendInitialMetadataIfNeeded(&buf);
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
class ClientAsyncReader final : public ClientAsyncStreamingInterface,
                           public AsyncReaderInterface<R> {
 public:
  // Blocking create a stream and write the first request out.
  ClientAsyncReader(ChannelInterface *channel, const RpcMethod &method,
               ClientContext *context,
               const google::protobuf::Message &request, void* tag)
      : call_(channel->CreateCall(method, context, &cq_)) {
    init_buf_.Reset(tag);
    init_buf_.AddSendMessage(request);
    init_buf_.AddClientSendClose();
    call_.PerformOps(&init_buf_);
  }

  virtual void Read(R *msg, void* tag) override {
    read_buf_.Reset(tag);
    read_buf_.AddRecvMessage(msg);
    call_.PerformOps(&read_buf_);
  }

  virtual void Finish(Status* status, void* tag) override {
    finish_buf_.Reset(tag);
    finish_buf_.AddClientRecvStatus(nullptr, status);  // TODO metadata
    call_.PerformOps(&finish_buf_);
  }

 private:
  CompletionQueue cq_;
  Call call_;
  CallOpBuffer init_buf_;
  CallOpBuffer read_buf_;
  CallOpBuffer finish_buf_;
};

template <class W>
class ClientAsyncWriter final : public ClientAsyncStreamingInterface,
                           public WriterInterface<W> {
 public:
  // Blocking create a stream.
  ClientAsyncWriter(ChannelInterface *channel, const RpcMethod &method,
               ClientContext *context,
               google::protobuf::Message *response)
      : response_(response),
        call_(channel->CreateCall(method, context, &cq_)) {}

  virtual void Write(const W& msg, void* tag) override {
    write_buf_.Reset(tag);
    write_buf_.AddSendMessage(msg);
    call_.PerformOps(&write_buf_);
  }

  virtual void WritesDone(void* tag) override {
    writes_done_buf_.Reset(tag);
    writes_done_buf_.AddClientSendClose();
    call_.PerformOps(&writes_done_buf_);
  }

  virtual void Finish(Status* status, void* tag) override {
    finish_buf_.Reset(tag);
    finish_buf_.AddRecvMessage(response_, &got_message_);
    finish_buf_.AddClientRecvStatus(nullptr, status);  // TODO metadata
    call_.PerformOps(&finish_buf_);
  }

 private:
  google::protobuf::Message *const response_;
  bool got_message_;
  CompletionQueue cq_;
  Call call_;
  CallOpBuffer write_buf_;
  CallOpBuffer writes_done_buf_;
  CallOpBuffer finish_buf_;
};

// Client-side interface for bi-directional streaming.
template <class W, class R>
class ClientAsyncReaderWriter final : public ClientAsyncStreamingInterface,
                                 public AsyncWriterInterface<W>,
                                 public AsyncReaderInterface<R> {
 public:
  ClientAsyncReaderWriter(ChannelInterface *channel,
                     const RpcMethod &method, ClientContext *context)
      : call_(channel->CreateCall(method, context, &cq_)) {}

  virtual void Read(R *msg, void* tag) override {
    read_buf_.Reset(tag);
    read_buf_.AddRecvMessage(msg);
    call_.PerformOps(&read_buf_);
  }

  virtual void Write(const W& msg, void* tag) override {
    write_buf_.Reset(tag);
    write_buf_.AddSendMessage(msg);
    call_.PerformOps(&write_buf_);
  }

  virtual void WritesDone(void* tag) override {
    writes_done_buf_.Reset(tag);
    writes_done_buf_.AddClientSendClose();
    call_.PerformOps(&writes_done_buf_);
  }

  virtual void Finish(Status* status, void* tag) override {
    finish_buf_.Reset(tag);
    finish_buf_.AddClientRecvStatus(nullptr, status);  // TODO metadata
    call_.PerformOps(&finish_buf_);
  }

 private:
  CompletionQueue cq_;
  Call call_;
  CallOpBuffer read_buf_;
  CallOpBuffer write_buf_;
  CallOpBuffer writes_done_buf_;
  CallOpBuffer finish_buf_;
};

// TODO(yangg) Move out of stream.h
template <class W>
class ServerAsyncResponseWriter final {
 public:
  explicit ServerAsyncResponseWriter(Call* call) : call_(call) {}

  virtual void Write(const W& msg, void* tag) override {
    CallOpBuffer buf;
    buf.AddSendMessage(msg);
    call_->PerformOps(&buf);
  }

 private:
  Call* call_;
};

template <class R>
class ServerAsyncReader : public AsyncReaderInterface<R> {
 public:
  explicit ServerAsyncReader(Call* call) : call_(call) {}

  virtual void Read(R* msg, void* tag) {
    // TODO
  }

 private:
  Call* call_;
};

template <class W>
class ServerAsyncWriter : public AsyncWriterInterface<W> {
 public:
  explicit ServerAsyncWriter(Call* call) : call_(call) {}

  virtual void Write(const W& msg, void* tag) {
    // TODO
  }

 private:
  Call* call_;
};

// Server-side interface for bi-directional streaming.
template <class W, class R>
class ServerAsyncReaderWriter : public AsyncWriterInterface<W>,
                           public AsyncReaderInterface<R> {
 public:
  explicit ServerAsyncReaderWriter(Call* call) : call_(call) {}

  virtual void Read(R* msg, void* tag) {
    // TODO
  }

  virtual void Write(const W& msg, void* tag) {
    // TODO
  }

 private:
  Call* call_;
};

}  // namespace grpc

#endif  // __GRPCPP_STREAM_H__
