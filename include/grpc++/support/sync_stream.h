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

#ifndef GRPCXX_SUPPORT_SYNC_STREAM_H
#define GRPCXX_SUPPORT_SYNC_STREAM_H

#include <grpc/support/log.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/impl/call.h>
#include <grpc++/impl/service_type.h>
#include <grpc++/server_context.h>
#include <grpc++/support/status.h>

namespace grpc {

/// Common interface for all synchronous client side streaming.
class ClientStreamingInterface {
 public:
  virtual ~ClientStreamingInterface() {}

  /// Wait until the stream finishes, and return the final status. When the
  /// client side declares it has no more message to send, either implicitly or
  /// by calling \a WritesDone(), it needs to make sure there is no more message
  /// to be received from the server, either implicitly or by getting a false
  /// from a \a Read().
  ///
  /// This function will return either:
  /// - when all incoming messages have been read and the server has returned
  ///   status.
  /// - OR when the server has returned a non-OK status.
  virtual Status Finish() = 0;
};

/// An interface that yields a sequence of messages of type \a R.
template <class R>
class ReaderInterface {
 public:
  virtual ~ReaderInterface() {}

  /// Blocking read a message and parse to \a msg. Returns \a true on success.
  ///
  /// \param[out] msg The read message.
  ///
  /// \return \a false when there will be no more incoming messages, either
  /// because the other side has called \a WritesDone() or the stream has failed
  /// (or been cancelled).
  virtual bool Read(R* msg) = 0;
};

/// An interface that can be fed a sequence of messages of type \a W.
template <class W>
class WriterInterface {
 public:
  virtual ~WriterInterface() {}

  /// Blocking write \a msg to the stream with options.
  ///
  /// \param msg The message to be written to the stream.
  /// \param options Options affecting the write operation.
  ///
  /// \return \a true on success, \a false when the stream has been closed.
  virtual bool Write(const W& msg, const WriteOptions& options) = 0;

  /// Blocking write \a msg to the stream with default options.
  ///
  /// \param msg The message to be written to the stream.
  ///
  /// \return \a true on success, \a false when the stream has been closed.
  inline bool Write(const W& msg) { return Write(msg, WriteOptions()); }
};

/// Client-side interface for streaming reads of message of type \a R.
template <class R>
class ClientReaderInterface : public ClientStreamingInterface,
                              public ReaderInterface<R> {
 public:
  /// Blocking wait for initial metadata from server. The received metadata
  /// can only be accessed after this call returns. Should only be called before
  /// the first read. Calling this method is optional, and if it is not called
  /// the metadata will be available in ClientContext after the first read.
  virtual void WaitForInitialMetadata() = 0;
};

template <class R>
class ClientReader GRPC_FINAL : public ClientReaderInterface<R> {
 public:
  /// Blocking create a stream and write the first request out.
  template <class W>
  ClientReader(Channel* channel, const RpcMethod& method,
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

  void WaitForInitialMetadata() GRPC_OVERRIDE {
    GPR_ASSERT(!context_->initial_metadata_received_);

    CallOpSet<CallOpRecvInitialMetadata> ops;
    ops.RecvInitialMetadata(context_);
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);  /// status ignored
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

/// Client-side interface for streaming writes of message of type \a W.
template <class W>
class ClientWriterInterface : public ClientStreamingInterface,
                              public WriterInterface<W> {
 public:
  /// Half close writing from the client.
  /// Block until writes are completed.
  ///
  /// \return Whether the writes were successful.
  virtual bool WritesDone() = 0;
};

template <class W>
class ClientWriter : public ClientWriterInterface<W> {
 public:
  /// Blocking create a stream.
  template <class R>
  ClientWriter(Channel* channel, const RpcMethod& method,
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

  /// Read the final response and wait for the final status.
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

/// Client-side interface for bi-directional streaming.
template <class W, class R>
class ClientReaderWriterInterface : public ClientStreamingInterface,
                                    public WriterInterface<W>,
                                    public ReaderInterface<R> {
 public:
  /// Blocking wait for initial metadata from server. The received metadata
  /// can only be accessed after this call returns. Should only be called before
  /// the first read. Calling this method is optional, and if it is not called
  /// the metadata will be available in ClientContext after the first read.
  virtual void WaitForInitialMetadata() = 0;

  /// Block until writes are completed.
  ///
  /// \return Whether the writes were successful.
  virtual bool WritesDone() = 0;
};

template <class W, class R>
class ClientReaderWriter GRPC_FINAL : public ClientReaderWriterInterface<W, R> {
 public:
  /// Blocking create a stream.
  ClientReaderWriter(Channel* channel, const RpcMethod& method,
                     ClientContext* context)
      : context_(context), call_(channel->CreateCall(method, context, &cq_)) {
    CallOpSet<CallOpSendInitialMetadata> ops;
    ops.SendInitialMetadata(context->send_initial_metadata_);
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);
  }

  void WaitForInitialMetadata() GRPC_OVERRIDE {
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

/// Server-side interface for bi-directional streaming.
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

}  // namespace grpc

#endif  // GRPCXX_SUPPORT_SYNC_STREAM_H
