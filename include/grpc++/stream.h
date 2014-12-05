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

#include <grpc++/stream_context_interface.h>
#include <grpc++/status.h>
#include <grpc/support/log.h>

namespace grpc {

// Common interface for all client side streaming.
class ClientStreamingInterface {
 public:
  virtual ~ClientStreamingInterface() {}

  // Try to cancel the stream. Wait() still needs to be called to get the final
  // status. Cancelling after the stream has finished has no effects.
  virtual void Cancel() = 0;

  // Wait until the stream finishes, and return the final status. When the
  // client side declares it has no more message to send, either implicitly or
  // by calling WritesDone, it needs to make sure there is no more message to
  // be received from the server, either implicitly or by getting a false from
  // a Read(). Otherwise, this implicitly cancels the stream.
  virtual const Status& Wait() = 0;
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
class ClientReader : public ClientStreamingInterface,
                     public ReaderInterface<R> {
 public:
  // Blocking create a stream and write the first request out.
  explicit ClientReader(StreamContextInterface* context) : context_(context) {
    GPR_ASSERT(context_);
    context_->Start(true);
    context_->Write(context_->request(), true);
  }

  ~ClientReader() { delete context_; }

  virtual bool Read(R* msg) { return context_->Read(msg); }

  virtual void Cancel() { context_->FinishStream(Status::Cancelled, true); }

  virtual const Status& Wait() { return context_->Wait(); }

 private:
  StreamContextInterface* const context_;
};

template <class W>
class ClientWriter : public ClientStreamingInterface,
                     public WriterInterface<W> {
 public:
  // Blocking create a stream.
  explicit ClientWriter(StreamContextInterface* context) : context_(context) {
    GPR_ASSERT(context_);
    context_->Start(false);
  }

  ~ClientWriter() { delete context_; }

  virtual bool Write(const W& msg) {
    return context_->Write(const_cast<W*>(&msg), false);
  }

  virtual void WritesDone() { context_->Write(nullptr, true); }

  virtual void Cancel() { context_->FinishStream(Status::Cancelled, true); }

  // Read the final response and wait for the final status.
  virtual const Status& Wait() {
    bool success = context_->Read(context_->response());
    if (!success) {
      Cancel();
    } else {
      success = context_->Read(nullptr);
      if (success) {
        Cancel();
      }
    }
    return context_->Wait();
  }

 private:
  StreamContextInterface* const context_;
};

// Client-side interface for bi-directional streaming.
template <class W, class R>
class ClientReaderWriter : public ClientStreamingInterface,
                           public WriterInterface<W>,
                           public ReaderInterface<R> {
 public:
  // Blocking create a stream.
  explicit ClientReaderWriter(StreamContextInterface* context)
      : context_(context) {
    GPR_ASSERT(context_);
    context_->Start(false);
  }

  ~ClientReaderWriter() { delete context_; }

  virtual bool Read(R* msg) { return context_->Read(msg); }

  virtual bool Write(const W& msg) {
    return context_->Write(const_cast<W*>(&msg), false);
  }

  virtual void WritesDone() { context_->Write(nullptr, true); }

  virtual void Cancel() { context_->FinishStream(Status::Cancelled, true); }

  virtual const Status& Wait() { return context_->Wait(); }

 private:
  StreamContextInterface* const context_;
};

template <class R>
class ServerReader : public ReaderInterface<R> {
 public:
  explicit ServerReader(StreamContextInterface* context) : context_(context) {
    GPR_ASSERT(context_);
    context_->Start(true);
  }

  virtual bool Read(R* msg) { return context_->Read(msg); }

 private:
  StreamContextInterface* const context_;  // not owned
};

template <class W>
class ServerWriter : public WriterInterface<W> {
 public:
  explicit ServerWriter(StreamContextInterface* context) : context_(context) {
    GPR_ASSERT(context_);
    context_->Start(true);
    context_->Read(context_->request());
  }

  virtual bool Write(const W& msg) {
    return context_->Write(const_cast<W*>(&msg), false);
  }

 private:
  StreamContextInterface* const context_;  // not owned
};

// Server-side interface for bi-directional streaming.
template <class W, class R>
class ServerReaderWriter : public WriterInterface<W>,
                           public ReaderInterface<R> {
 public:
  explicit ServerReaderWriter(StreamContextInterface* context)
      : context_(context) {
    GPR_ASSERT(context_);
    context_->Start(true);
  }

  virtual bool Read(R* msg) { return context_->Read(msg); }

  virtual bool Write(const W& msg) {
    return context_->Write(const_cast<W*>(&msg), false);
  }

 private:
  StreamContextInterface* const context_;  // not owned
};

}  // namespace grpc

#endif  // __GRPCPP_STREAM_H__
