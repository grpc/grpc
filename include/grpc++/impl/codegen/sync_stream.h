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

#ifndef GRPCXX_IMPL_CODEGEN_SYNC_STREAM_H
#define GRPCXX_IMPL_CODEGEN_SYNC_STREAM_H

#include <grpc++/impl/codegen/call.h>
#include <grpc++/impl/codegen/channel_interface.h>
#include <grpc++/impl/codegen/client_context.h>
#include <grpc++/impl/codegen/completion_queue.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/server_context.h>
#include <grpc++/impl/codegen/service_type.h>
#include <grpc++/impl/codegen/status.h>

namespace grpc {

enum StreamOpStatus { FAIL, SUCCESS, TIMEOUT };

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
  /// - OR with a timeout if the stream ops haven't finished before the deadline
  ///   specified in FinishOptions. In this case, the Status return value is
  ///   not valid and should not be used
  virtual Status Finish(const FinishOptions& options,
                        StreamOpStatus* completed) = 0;

  /// A default version of Finish that doesn't set a deadline or options
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
  inline Status Finish() {
    StreamOpStatus dummy;
    return Finish(FinishOptions(), &dummy);
  }
};

/// Common interface for all synchronous server side streaming.
class ServerStreamingInterface {
 public:
  virtual ~ServerStreamingInterface() {}

  /// Blocking send initial metadata to client with options
  virtual StreamOpStatus SendInitialMetadata(
      const SendInitialMetadataOptions& options) = 0;

  /// Default version of SendInitialMetadata that doesn't have options
  inline void SendInitialMetadata() {
    SendInitialMetadata(SendInitialMetadataOptions());
  }
};

/// An interface that yields a sequence of messages of type \a R.
template <class R>
class ReaderInterface {
 public:
  virtual ~ReaderInterface() {}

  /// Upper bound on the next message size available for reading on this stream
  virtual StreamOpStatus NextMessageSize(
      uint32_t* sz, const NextMessageSizeOptions& options) = 0;

  /// Default version of NextMessageSize with default options
  inline bool NextMessageSize(uint32_t* sz) {
    return NextMessageSize(sz, NextMessageSizeOptions()) ==
           StreamOpStatus::SUCCESS;
  }

  /// Blocking read a message and parse to \a msg. Returns \a true on success.
  /// This is thread-safe with respect to \a Write or \WritesDone methods on
  /// the same stream. It should not be called concurrently with another \a
  /// Read on the same stream as the order of delivery will not be defined.
  ///
  /// \param[out] msg The read message.
  ///
  /// \return \a FAIL when there will be no more incoming messages, either
  /// because the other side has called \a WritesDone() or the stream has failed
  /// (or been cancelled), \a TIMEOUT if there's a timeout, or \a SUCCESS
  /// if we got what we wanted
  virtual StreamOpStatus Read(R* msg, const ReadOptions& options) = 0;

  /// Blocking read a message and parse to \a msg. Returns \a true on success.
  /// This is thread-safe with respect to \a Write or \WritesDone methods on
  /// the same stream. It should not be called concurrently with another \a
  /// Read on the same stream as the order of delivery will not be defined.
  ///
  /// \param[out] msg The read message.
  ///
  /// \return \a false when there will be no more incoming messages, either
  /// because the other side has called \a WritesDone() or the stream has failed
  /// (or been cancelled).
  /// default version with default options
  inline bool Read(R* msg) {
    return Read(msg, ReadOptions()) == StreamOpStatus::SUCCESS;
  }
};

/// An interface that can be fed a sequence of messages of type \a W.
template <class W>
class WriterInterface {
 public:
  virtual ~WriterInterface() {}

  /// Blocking write \a msg to the stream with options.
  /// This is thread-safe with respect to \a Read
  ///
  /// \param msg The message to be written to the stream.
  /// \param options Options affecting the write operation.
  ///
  /// \return \a true on success, \a false when the stream has been closed.
  virtual StreamOpStatus Write(const W& msg, const WriteOptions& options) = 0;

  /// Blocking write \a msg to the stream with default options.
  /// This is thread-safe with respect to \a Read
  ///
  /// \param msg The message to be written to the stream.
  ///
  /// \return \a true on success, \a false when the stream has been closed.
  inline bool Write(const W& msg) {
    return Write(msg, WriteOptions()) == StreamOpStatus::SUCCESS;
  }
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
  virtual StreamOpStatus WaitForInitialMetadata(
      const WaitForInitialMetadataOptions& options) = 0;

  inline void WaitForInitialMetadata() {
    WaitForInitialMetadata(WaitForInitialMetadataOptions());
  }
};

template <class R>
class ClientReader final : public ClientReaderInterface<R> {
 public:
  /// Blocking create a stream and write the first request out.
  template <class W>
  ClientReader(ChannelInterface* channel, const RpcMethod& method,
               ClientContext* context, const W& request)
      : context_(context), call_(channel->CreateCall(method, context, &cq_)) {
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpClientSendClose>
        ops;
    ops.SendInitialMetadata(context->send_initial_metadata_,
                            context->initial_metadata_flags());
    // TODO(ctiller): don't assert
    GPR_CODEGEN_ASSERT(ops.SendMessage(request).ok());
    ops.ClientSendClose();
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);
  }

  using ClientReaderInterface<R>::WaitForInitialMetadata;

  StreamOpStatus WaitForInitialMetadata(
      const WaitForInitialMetadataOptions& options) override {
    GPR_CODEGEN_ASSERT(!context_->initial_metadata_received_);

    CallOpSet<CallOpRecvInitialMetadata> ops;
    ops.RecvInitialMetadata(context_);
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);  /// status ignored
    return StreamOpStatus::SUCCESS;
  }

  using ReaderInterface<R>::NextMessageSize;
  StreamOpStatus NextMessageSize(
      uint32_t* sz, const NextMessageSizeOptions& options) override {
    *sz = call_.max_receive_message_size();
    return StreamOpStatus::SUCCESS;
  }

  using ReaderInterface<R>::Read;
  StreamOpStatus Read(R* msg, const ReadOptions& options) override {
    CallOpSet<CallOpRecvInitialMetadata, CallOpRecvMessage<R>> ops;
    if (!context_->initial_metadata_received_) {
      ops.RecvInitialMetadata(context_);
    }
    ops.RecvMessage(msg);
    call_.PerformOps(&ops);
    return (cq_.Pluck(&ops) && ops.got_message) ? StreamOpStatus::SUCCESS
                                                : StreamOpStatus::FAIL;
  }

  using ClientStreamingInterface::Finish;

  Status Finish(const FinishOptions& options,
                StreamOpStatus* completed) override {
    CallOpSet<CallOpClientRecvStatus> ops;
    Status status;
    ops.ClientRecvStatus(context_, &status);
    call_.PerformOps(&ops);
    GPR_CODEGEN_ASSERT(cq_.Pluck(&ops));
    *completed = StreamOpStatus::SUCCESS;
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
  /// Block until currently-pending writes are completed.
  /// Thread safe with respect to \a Read operations only
  ///
  /// \return Whether the writes were successful.
  virtual StreamOpStatus WritesDone(const WritesDoneOptions& options) = 0;

  inline bool WritesDone() {
    return WritesDone(WritesDoneOptions()) == StreamOpStatus::SUCCESS;
  }
};

template <class W>
class ClientWriter : public ClientWriterInterface<W> {
 public:
  /// Blocking create a stream.
  template <class R>
  ClientWriter(ChannelInterface* channel, const RpcMethod& method,
               ClientContext* context, R* response)
      : context_(context), call_(channel->CreateCall(method, context, &cq_)) {
    finish_ops_.RecvMessage(response);
    finish_ops_.AllowNoMessage();

    CallOpSet<CallOpSendInitialMetadata> ops;
    ops.SendInitialMetadata(context->send_initial_metadata_,
                            context->initial_metadata_flags());
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);
  }

  StreamOpStatus WaitForInitialMetadata(
      const WaitForInitialMetadataOptions& options) {
    GPR_CODEGEN_ASSERT(!context_->initial_metadata_received_);

    CallOpSet<CallOpRecvInitialMetadata> ops;
    ops.RecvInitialMetadata(context_);
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);  // status ignored
    return StreamOpStatus::SUCCESS;
  }

  inline void WaitForInitialMetadata() {
    WaitForInitialMetadata(WaitForInitialMetadataOptions());
  }

  using WriterInterface<W>::Write;
  StreamOpStatus Write(const W& msg,
                       const WriteOptions& options) override {
    CallOpSet<CallOpSendMessage> ops;
    if (!ops.SendMessage(msg, options).ok()) {
      return StreamOpStatus::FAIL;
    }
    call_.PerformOps(&ops);
    return (cq_.Pluck(&ops)) ? StreamOpStatus::SUCCESS : StreamOpStatus::FAIL;
  }

  using ClientWriterInterface<W>::WritesDone;
  StreamOpStatus WritesDone(const WritesDoneOptions& options) override {
    CallOpSet<CallOpClientSendClose> ops;
    ops.ClientSendClose();
    call_.PerformOps(&ops);
    return (cq_.Pluck(&ops)) ? StreamOpStatus::SUCCESS : StreamOpStatus::FAIL;
  }

  using ClientStreamingInterface::Finish;

  /// Read the final response and wait for the final status.
  Status Finish(const FinishOptions& options,
                StreamOpStatus* completed) override {
    Status status;
    if (!context_->initial_metadata_received_) {
      finish_ops_.RecvInitialMetadata(context_);
    }
    finish_ops_.ClientRecvStatus(context_, &status);
    call_.PerformOps(&finish_ops_);
    GPR_CODEGEN_ASSERT(cq_.Pluck(&finish_ops_));
    *completed = StreamOpStatus::SUCCESS;
    return status;
  }

 private:
  ClientContext* context_;
  CallOpSet<CallOpRecvInitialMetadata, CallOpGenericRecvMessage,
            CallOpClientRecvStatus>
      finish_ops_;
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
  virtual StreamOpStatus WaitForInitialMetadata(
      const WaitForInitialMetadataOptions& options) = 0;

  inline void WaitForInitialMetadata() {
    WaitForInitialMetadata(WaitForInitialMetadataOptions());
  }

  /// Block until currently-pending writes are completed.
  /// Thread-safe with respect to \a Read
  ///
  /// \return Whether the writes were successful.
  virtual StreamOpStatus WritesDone(const WritesDoneOptions& options) = 0;

  inline bool WritesDone() {
    return WritesDone(WritesDoneOptions()) == StreamOpStatus::SUCCESS;
  }
};

template <class W, class R>
class ClientReaderWriter final : public ClientReaderWriterInterface<W, R> {
 public:
  /// Blocking create a stream.
  ClientReaderWriter(ChannelInterface* channel, const RpcMethod& method,
                     ClientContext* context)
      : context_(context), call_(channel->CreateCall(method, context, &cq_)) {
    CallOpSet<CallOpSendInitialMetadata> ops;
    ops.SendInitialMetadata(context->send_initial_metadata_,
                            context->initial_metadata_flags());
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);
  }

  using ClientReaderWriterInterface<W, R>::WaitForInitialMetadata;
  StreamOpStatus WaitForInitialMetadata(
      const WaitForInitialMetadataOptions& options) override {
    GPR_CODEGEN_ASSERT(!context_->initial_metadata_received_);

    CallOpSet<CallOpRecvInitialMetadata> ops;
    ops.RecvInitialMetadata(context_);
    call_.PerformOps(&ops);
    cq_.Pluck(&ops);  // status ignored
    return StreamOpStatus::SUCCESS;
  }

  using ReaderInterface<R>::NextMessageSize;
  StreamOpStatus NextMessageSize(
      uint32_t* sz, const NextMessageSizeOptions& options) override {
    *sz = call_.max_receive_message_size();
    return StreamOpStatus::SUCCESS;
  }

  using ReaderInterface<R>::Read;
  StreamOpStatus Read(R* msg, const ReadOptions& options) override {
    CallOpSet<CallOpRecvInitialMetadata, CallOpRecvMessage<R>> ops;
    if (!context_->initial_metadata_received_) {
      ops.RecvInitialMetadata(context_);
    }
    ops.RecvMessage(msg);
    call_.PerformOps(&ops);
    return (cq_.Pluck(&ops) && ops.got_message) ? StreamOpStatus::SUCCESS
                                                : StreamOpStatus::FAIL;
  }

  using WriterInterface<W>::Write;
  StreamOpStatus Write(const W& msg,
                       const WriteOptions& options) override {
    CallOpSet<CallOpSendMessage> ops;
    if (!ops.SendMessage(msg, options).ok()) return StreamOpStatus::FAIL;
    call_.PerformOps(&ops);
    return (cq_.Pluck(&ops)) ? StreamOpStatus::SUCCESS : StreamOpStatus::FAIL;
  }

  using ClientReaderWriterInterface<W, R>::WritesDone;
  StreamOpStatus WritesDone(const WritesDoneOptions& options) override {
    CallOpSet<CallOpClientSendClose> ops;
    ops.ClientSendClose();
    call_.PerformOps(&ops);
    return (cq_.Pluck(&ops)) ? StreamOpStatus::SUCCESS : StreamOpStatus::FAIL;
  }

  using ClientStreamingInterface::Finish;

  Status Finish(const FinishOptions& options,
                StreamOpStatus* completed) override {
    CallOpSet<CallOpRecvInitialMetadata, CallOpClientRecvStatus> ops;
    if (!context_->initial_metadata_received_) {
      ops.RecvInitialMetadata(context_);
    }
    Status status;
    ops.ClientRecvStatus(context_, &status);
    call_.PerformOps(&ops);
    GPR_CODEGEN_ASSERT(cq_.Pluck(&ops));
    *completed = StreamOpStatus::SUCCESS;
    return status;
  }

 private:
  ClientContext* context_;
  CompletionQueue cq_;
  Call call_;
};

/// Server-side interface for streaming reads of message of type \a R.
template <class R>
class ServerReaderInterface : public ServerStreamingInterface,
                              public ReaderInterface<R> {};

template <class R>
class ServerReader final : public ServerReaderInterface<R> {
 public:
  ServerReader(Call* call, ServerContext* ctx) : call_(call), ctx_(ctx) {}

  StreamOpStatus SendInitialMetadata(const SendInitialMetadataOptions& options)
      override {
    GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);

    CallOpSet<CallOpSendInitialMetadata> ops;
    ops.SendInitialMetadata(ctx_->initial_metadata_,
                            ctx_->initial_metadata_flags());
    if (ctx_->compression_level_set()) {
      ops.set_compression_level(ctx_->compression_level());
    }
    ctx_->sent_initial_metadata_ = true;
    call_->PerformOps(&ops);
    call_->cq()->Pluck(&ops);
    return StreamOpStatus::SUCCESS;
  }

  using ReaderInterface<R>::NextMessageSize;
  StreamOpStatus NextMessageSize(
      uint32_t* sz, const NextMessageSizeOptions& options) override {
    *sz = call_->max_receive_message_size();
    return StreamOpStatus::SUCCESS;
  }

  using ReaderInterface<R>::Read;
  StreamOpStatus Read(R* msg, const ReadOptions& options) override {
    CallOpSet<CallOpRecvMessage<R>> ops;
    ops.RecvMessage(msg);
    call_->PerformOps(&ops);
    return (call_->cq()->Pluck(&ops) && ops.got_message)
               ? StreamOpStatus::SUCCESS
               : StreamOpStatus::FAIL;
  }

 private:
  Call* const call_;
  ServerContext* const ctx_;
};

/// Server-side interface for streaming writes of message of type \a W.
template <class W>
class ServerWriterInterface : public ServerStreamingInterface,
                              public WriterInterface<W> {};

template <class W>
class ServerWriter final : public ServerWriterInterface<W> {
 public:
  ServerWriter(Call* call, ServerContext* ctx) : call_(call), ctx_(ctx) {}

  StreamOpStatus SendInitialMetadata(const SendInitialMetadataOptions& options)
      override {
    GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);

    CallOpSet<CallOpSendInitialMetadata> ops;
    ops.SendInitialMetadata(ctx_->initial_metadata_,
                            ctx_->initial_metadata_flags());
    if (ctx_->compression_level_set()) {
      ops.set_compression_level(ctx_->compression_level());
    }
    ctx_->sent_initial_metadata_ = true;
    call_->PerformOps(&ops);
    call_->cq()->Pluck(&ops);
    return StreamOpStatus::SUCCESS;
  }

  using WriterInterface<W>::Write;
  StreamOpStatus Write(const W& msg,
                       const WriteOptions& options) override {
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage> ops;
    if (!ops.SendMessage(msg, options).ok()) {
      return StreamOpStatus::FAIL;
    }
    if (!ctx_->sent_initial_metadata_) {
      ops.SendInitialMetadata(ctx_->initial_metadata_,
                              ctx_->initial_metadata_flags());
      if (ctx_->compression_level_set()) {
        ops.set_compression_level(ctx_->compression_level());
      }
      ctx_->sent_initial_metadata_ = true;
    }
    call_->PerformOps(&ops);
    return (call_->cq()->Pluck(&ops)) ? StreamOpStatus::SUCCESS
                                      : StreamOpStatus::FAIL;
  }

 private:
  Call* const call_;
  ServerContext* const ctx_;
};

/// Server-side interface for bi-directional streaming.
template <class W, class R>
class ServerReaderWriterInterface : public ServerStreamingInterface,
                                    public WriterInterface<W>,
                                    public ReaderInterface<R> {};

// Actual implementation of bi-directional streaming
namespace internal {
template <class W, class R>
class ServerReaderWriterBody final {
 public:
  ServerReaderWriterBody(Call* call, ServerContext* ctx)
      : call_(call), ctx_(ctx) {}

  StreamOpStatus SendInitialMetadata(
      const SendInitialMetadataOptions& options) {
    GPR_CODEGEN_ASSERT(!ctx_->sent_initial_metadata_);

    CallOpSet<CallOpSendInitialMetadata> ops;
    ops.SendInitialMetadata(ctx_->initial_metadata_,
                            ctx_->initial_metadata_flags());
    if (ctx_->compression_level_set()) {
      ops.set_compression_level(ctx_->compression_level());
    }
    ctx_->sent_initial_metadata_ = true;
    call_->PerformOps(&ops);
    call_->cq()->Pluck(&ops);
    return StreamOpStatus::SUCCESS;
  }

  StreamOpStatus NextMessageSize(uint32_t* sz,
                                 const NextMessageSizeOptions& options) {
    *sz = call_->max_receive_message_size();
    return StreamOpStatus::SUCCESS;
  }

  StreamOpStatus Read(R* msg, const ReadOptions& options) {
    CallOpSet<CallOpRecvMessage<R>> ops;
    ops.RecvMessage(msg);
    call_->PerformOps(&ops);
    return (call_->cq()->Pluck(&ops) && ops.got_message)
               ? StreamOpStatus::SUCCESS
               : StreamOpStatus::FAIL;
  }

  StreamOpStatus Write(const W& msg, const WriteOptions& options) {
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage> ops;
    if (!ops.SendMessage(msg, options).ok()) {
      return StreamOpStatus::FAIL;
    }
    if (!ctx_->sent_initial_metadata_) {
      ops.SendInitialMetadata(ctx_->initial_metadata_,
                              ctx_->initial_metadata_flags());
      if (ctx_->compression_level_set()) {
        ops.set_compression_level(ctx_->compression_level());
      }
      ctx_->sent_initial_metadata_ = true;
    }
    call_->PerformOps(&ops);
    return (call_->cq()->Pluck(&ops)) ? StreamOpStatus::SUCCESS
                                      : StreamOpStatus::FAIL;
  }

 private:
  Call* const call_;
  ServerContext* const ctx_;
};
}

// class to represent the user API for a bidirectional streaming call
template <class W, class R>
class ServerReaderWriter final : public ServerReaderWriterInterface<W, R> {
 public:
  ServerReaderWriter(Call* call, ServerContext* ctx) : body_(call, ctx) {}

  StreamOpStatus SendInitialMetadata(const SendInitialMetadataOptions& options)
      override {
    return body_.SendInitialMetadata(options);
  }

  using ReaderInterface<R>::NextMessageSize;
  StreamOpStatus NextMessageSize(
      uint32_t* sz, const NextMessageSizeOptions& options) override {
    return body_.NextMessageSize(sz, options);
  }

  using ReaderInterface<R>::Read;
  StreamOpStatus Read(R* msg, const ReadOptions& options) override {
    return body_.Read(msg, options);
  }

  using WriterInterface<W>::Write;
  StreamOpStatus Write(const W& msg,
                       const WriteOptions& options) override {
    return body_.Write(msg, options);
  }

 private:
  internal::ServerReaderWriterBody<W, R> body_;
};

/// A class to represent a flow-controlled unary call. This is something
/// of a hybrid between conventional unary and streaming. This is invoked
/// through a unary call on the client side, but the server responds to it
/// as though it were a single-ping-pong streaming call. The server can use
/// the \a NextMessageSize method to determine an upper-bound on the size of
/// the message.
/// A key difference relative to streaming: ServerUnaryStreamer
/// must have exactly 1 Read and exactly 1 Write, in that order, to function
/// correctly. Otherwise, the RPC is in error.
template <class RequestType, class ResponseType>
class ServerUnaryStreamer final
    : public ServerReaderWriterInterface<ResponseType, RequestType> {
 public:
  ServerUnaryStreamer(Call* call, ServerContext* ctx)
      : body_(call, ctx), read_done_(false), write_done_(false) {}

  StreamOpStatus SendInitialMetadata(const SendInitialMetadataOptions& options)
      override {
    return body_.SendInitialMetadata(options);
  }

  using ReaderInterface<RequestType>::NextMessageSize;
  StreamOpStatus NextMessageSize(
      uint32_t* sz, const NextMessageSizeOptions& options) override {
    return body_.NextMessageSize(sz, options);
  }

  using ReaderInterface<RequestType>::Read;
  StreamOpStatus Read(RequestType* request,
                      const ReadOptions& options) override {
    if (read_done_) {
      return StreamOpStatus::FAIL;
    }
    read_done_ = true;
    return body_.Read(request, options);
  }

  using WriterInterface<ResponseType>::Write;
  StreamOpStatus Write(const ResponseType& response,
                       const WriteOptions& options) override {
    if (write_done_ || !read_done_) {
      return StreamOpStatus::FAIL;
    }
    write_done_ = true;
    return body_.Write(response, options);
  }

 private:
  internal::ServerReaderWriterBody<ResponseType, RequestType> body_;
  bool read_done_;
  bool write_done_;
};

/// A class to represent a flow-controlled server-side streaming call.
/// This is something of a hybrid between server-side and bidi streaming.
/// This is invoked through a server-side streaming call on the client side,
/// but the server responds to it as though it were a bidi streaming call that
/// must first have exactly 1 Read and then any number of Writes.
template <class RequestType, class ResponseType>
class ServerSplitStreamer final
    : public ServerReaderWriterInterface<ResponseType, RequestType> {
 public:
  ServerSplitStreamer(Call* call, ServerContext* ctx)
      : body_(call, ctx), read_done_(false) {}

  StreamOpStatus SendInitialMetadata(const SendInitialMetadataOptions& options)
      override {
    return body_.SendInitialMetadata(options);
  }

  using ReaderInterface<RequestType>::NextMessageSize;
  StreamOpStatus NextMessageSize(
      uint32_t* sz, const NextMessageSizeOptions& options) override {
    return body_.NextMessageSize(sz, options);
  }

  using ReaderInterface<RequestType>::Read;
  StreamOpStatus Read(RequestType* request,
                      const ReadOptions& options) override {
    if (read_done_) {
      return StreamOpStatus::FAIL;
    }
    read_done_ = true;
    return body_.Read(request, options);
  }

  using WriterInterface<ResponseType>::Write;
  StreamOpStatus Write(const ResponseType& response,
                       const WriteOptions& options) override {
    if (!read_done_) {
      return StreamOpStatus::FAIL;
    }
    return body_.Write(response, options);
  }

 private:
  internal::ServerReaderWriterBody<ResponseType, RequestType> body_;
  bool read_done_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_SYNC_STREAM_H
