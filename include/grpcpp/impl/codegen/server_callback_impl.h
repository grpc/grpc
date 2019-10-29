/*
 *
 * Copyright 2019 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_IMPL_H
#define GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_IMPL_H

#include <atomic>
#include <functional>
#include <type_traits>

#include <grpcpp/impl/codegen/call.h>
#include <grpcpp/impl/codegen/call_op_set.h>
#include <grpcpp/impl/codegen/callback_common.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/message_allocator.h>
#include <grpcpp/impl/codegen/status.h>

namespace grpc_impl {

// Declare base class of all reactors as internal
namespace internal {

// Forward declarations
template <class Request, class Response>
class CallbackUnaryHandler;
template <class Request, class Response>
class CallbackClientStreamingHandler;
template <class Request, class Response>
class CallbackServerStreamingHandler;
template <class Request, class Response>
class CallbackBidiHandler;

class ServerReactor {
 public:
  virtual ~ServerReactor() = default;
  virtual void OnDone() = 0;
  virtual void OnCancel() = 0;

  // The following is not API. It is for internal use only and specifies whether
  // all reactions of this Reactor can be run without an extra executor
  // scheduling. This should only be used for internally-defined reactors with
  // trivial reactions.
  virtual bool InternalInlineable() { return false; }

 private:
  template <class Request, class Response>
  friend class CallbackUnaryHandler;
  template <class Request, class Response>
  friend class CallbackClientStreamingHandler;
  template <class Request, class Response>
  friend class CallbackServerStreamingHandler;
  template <class Request, class Response>
  friend class CallbackBidiHandler;
};

/// The base class of ServerCallbackUnary etc.
class ServerCallbackCall {
 public:
  virtual ~ServerCallbackCall() {}

  // This object is responsible for tracking when it is safe to call
  // OnCancel. This function should not be called until after the method handler
  // is done and the RPC has completed with a cancellation. This is tracked by
  // counting how many of these conditions have been met and calling OnCancel
  // when none remain unmet.

  // Fast version called with known reactor passed in, used from derived
  // classes, typically in non-cancel case
  void MaybeCallOnCancel(ServerReactor* reactor, bool invoke_done) {
    if (GPR_UNLIKELY(on_cancel_conditions_remaining_.fetch_sub(
                         1, std::memory_order_acq_rel) == 1)) {
      CallOnCancel(reactor, invoke_done);
    } else if (invoke_done) {
      MaybeDone();
    }
  }

  // Slower version called from object that doesn't know the reactor a priori
  // (such as the ServerContext CompletionOp which is formed before the
  // reactor). This is used in cancel cases only, so it's ok to be slower and
  // invoke a virtual function.
  void MaybeCallOnCancel(bool invoke_done) {
    MaybeCallOnCancel(reactor(), invoke_done);
  }

 protected:
  /// Increases the reference count
  void Ref() { callbacks_outstanding_.fetch_add(1, std::memory_order_relaxed); }

  /// Decreases the reference count and returns the previous value
  int Unref() {
    return callbacks_outstanding_.fetch_sub(1, std::memory_order_acq_rel);
  }

 private:
  virtual ServerReactor* reactor() = 0;
  virtual void MaybeDone() = 0;

  // If the OnCancel reaction is inlineable, execute it inline. Otherwise send
  // it to an executor.
  void CallOnCancel(ServerReactor* reactor, bool invoke_done);

  std::atomic_int on_cancel_conditions_remaining_{2};
  std::atomic_int callbacks_outstanding_{
      3};  // reserve for start, Finish, and CompletionOp
};

template <class Request, class Response>
class DefaultMessageHolder
    : public ::grpc::experimental::MessageHolder<Request, Response> {
 public:
  DefaultMessageHolder() {
    this->set_request(&request_obj_);
    this->set_response(&response_obj_);
  }
  void Release() override {
    // the object is allocated in the call arena.
    this->~DefaultMessageHolder<Request, Response>();
  }

 private:
  Request request_obj_;
  Response response_obj_;
};

}  // namespace internal

namespace experimental {

// Forward declarations
class ServerUnaryReactor;
template <class Request>
class ServerReadReactor;
template <class Response>
class ServerWriteReactor;
template <class Request, class Response>
class ServerBidiReactor;

// NOTE: The actual call/stream object classes are provided as API only to
// support mocking. There are no implementations of these class interfaces in
// the API.
class ServerCallbackUnary : public internal::ServerCallbackCall {
 public:
  virtual ~ServerCallbackUnary() {}
  virtual void Finish(::grpc::Status s) = 0;
  virtual void SendInitialMetadata() = 0;

 protected:
  // Use a template rather than explicitly specifying ServerUnaryReactor to
  // delay binding and avoid a circular forward declaration issue
  template <class Reactor>
  void BindReactor(Reactor* reactor) {
    reactor->InternalBindCall(this);
  }
};

template <class Request>
class ServerCallbackReader : public internal::ServerCallbackCall {
 public:
  virtual ~ServerCallbackReader() {}
  virtual void Finish(::grpc::Status s) = 0;
  virtual void SendInitialMetadata() = 0;
  virtual void Read(Request* msg) = 0;

 protected:
  void BindReactor(ServerReadReactor<Request>* reactor) {
    reactor->InternalBindReader(this);
  }
};

template <class Response>
class ServerCallbackWriter : public internal::ServerCallbackCall {
 public:
  virtual ~ServerCallbackWriter() {}

  virtual void Finish(::grpc::Status s) = 0;
  virtual void SendInitialMetadata() = 0;
  virtual void Write(const Response* msg, ::grpc::WriteOptions options) = 0;
  virtual void WriteAndFinish(const Response* msg, ::grpc::WriteOptions options,
                              ::grpc::Status s) = 0;

 protected:
  void BindReactor(ServerWriteReactor<Response>* reactor) {
    reactor->InternalBindWriter(this);
  }
};

template <class Request, class Response>
class ServerCallbackReaderWriter : public internal::ServerCallbackCall {
 public:
  virtual ~ServerCallbackReaderWriter() {}

  virtual void Finish(::grpc::Status s) = 0;
  virtual void SendInitialMetadata() = 0;
  virtual void Read(Request* msg) = 0;
  virtual void Write(const Response* msg, ::grpc::WriteOptions options) = 0;
  virtual void WriteAndFinish(const Response* msg, ::grpc::WriteOptions options,
                              ::grpc::Status s) = 0;

 protected:
  void BindReactor(ServerBidiReactor<Request, Response>* reactor) {
    reactor->InternalBindStream(this);
  }
};

// The following classes are the reactor interfaces that are to be implemented
// by the user, returned as the output parameter of the method handler for a
// callback method. Note that none of the classes are pure; all reactions have a
// default empty reaction so that the user class only needs to override those
// classes that it cares about.

/// \a ServerBidiReactor is the interface for a bidirectional streaming RPC.
template <class Request, class Response>
class ServerBidiReactor : public internal::ServerReactor {
 public:
  // NOTE: Initializing stream_ as a constructor initializer rather than a
  //       default initializer because gcc-4.x requires a copy constructor for
  //       default initializing a templated member, which isn't ok for atomic.
  // TODO(vjpai): Switch to default constructor and default initializer when
  //              gcc-4.x is no longer supported
  ServerBidiReactor() : stream_(nullptr) {}
  ~ServerBidiReactor() = default;

  /// Send any initial metadata stored in the RPC context. If not invoked,
  /// any initial metadata will be passed along with the first Write or the
  /// Finish (if there are no writes).
  void StartSendInitialMetadata() {
    auto* stream = stream_.load(std::memory_order_acquire);
    if (stream == nullptr) {
      grpc::internal::MutexLock l(&stream_mu_);
      stream = stream_.load(std::memory_order_acquire);
      if (stream == nullptr) {
        send_initial_metadata_wanted_ = true;
        return;
      }
    }
    stream->SendInitialMetadata();
  }

  /// Initiate a read operation.
  ///
  /// \param[out] req Where to eventually store the read message. Valid when
  ///                 the library calls OnReadDone
  void StartRead(Request* req) {
    auto* stream = stream_.load(std::memory_order_acquire);
    if (stream == nullptr) {
      grpc::internal::MutexLock l(&stream_mu_);
      stream = stream_.load(std::memory_order_acquire);
      if (stream == nullptr) {
        read_wanted_ = req;
        return;
      }
    }
    stream->Read(req);
  }

  /// Initiate a write operation.
  ///
  /// \param[in] resp The message to be written. The library takes temporary
  ///                 ownership until OnWriteDone, at which point the
  ///                 application regains ownership of resp.
  void StartWrite(const Response* resp) {
    StartWrite(resp, ::grpc::WriteOptions());
  }

  /// Initiate a write operation with specified options.
  ///
  /// \param[in] resp The message to be written. The library takes temporary
  ///                 ownership until OnWriteDone, at which point the
  ///                 application regains ownership of resp.
  /// \param[in] options The WriteOptions to use for writing this message
  void StartWrite(const Response* resp, ::grpc::WriteOptions options) {
    auto* stream = stream_.load(std::memory_order_acquire);
    if (stream == nullptr) {
      grpc::internal::MutexLock l(&stream_mu_);
      stream = stream_.load(std::memory_order_acquire);
      if (stream == nullptr) {
        write_wanted_ = resp;
        write_options_wanted_ = std::move(options);
        return;
      }
    }
    stream->Write(resp, std::move(options));
  }

  /// Initiate a write operation with specified options and final RPC Status,
  /// which also causes any trailing metadata for this RPC to be sent out.
  /// StartWriteAndFinish is like merging StartWriteLast and Finish into a
  /// single step. A key difference, though, is that this operation doesn't have
  /// an OnWriteDone reaction - it is considered complete only when OnDone is
  /// available. An RPC can either have StartWriteAndFinish or Finish, but not
  /// both.
  ///
  /// \param[in] resp The message to be written. The library takes temporary
  ///                 ownership until OnWriteDone, at which point the
  ///                 application regains ownership of resp.
  /// \param[in] options The WriteOptions to use for writing this message
  /// \param[in] s The status outcome of this RPC
  void StartWriteAndFinish(const Response* resp, ::grpc::WriteOptions options,
                           ::grpc::Status s) {
    auto* stream = stream_.load(std::memory_order_acquire);
    if (stream == nullptr) {
      grpc::internal::MutexLock l(&stream_mu_);
      stream = stream_.load(std::memory_order_acquire);
      if (stream == nullptr) {
        write_and_finish_wanted_ = true;
        write_wanted_ = resp;
        write_options_wanted_ = std::move(options);
        status_wanted_ = std::move(s);
        return;
      }
    }
    stream->WriteAndFinish(resp, std::move(options), std::move(s));
  }

  /// Inform system of a planned write operation with specified options, but
  /// allow the library to schedule the actual write coalesced with the writing
  /// of trailing metadata (which takes place on a Finish call).
  ///
  /// \param[in] resp The message to be written. The library takes temporary
  ///                 ownership until OnWriteDone, at which point the
  ///                 application regains ownership of resp.
  /// \param[in] options The WriteOptions to use for writing this message
  void StartWriteLast(const Response* resp, ::grpc::WriteOptions options) {
    StartWrite(resp, std::move(options.set_last_message()));
  }

  /// Indicate that the stream is to be finished and the trailing metadata and
  /// RPC status are to be sent. Every RPC MUST be finished using either Finish
  /// or StartWriteAndFinish (but not both), even if the RPC is already
  /// cancelled.
  ///
  /// \param[in] s The status outcome of this RPC
  void Finish(::grpc::Status s) {
    auto* stream = stream_.load(std::memory_order_acquire);
    if (stream == nullptr) {
      grpc::internal::MutexLock l(&stream_mu_);
      stream = stream_.load(std::memory_order_acquire);
      if (stream == nullptr) {
        finish_wanted_ = true;
        status_wanted_ = std::move(s);
        return;
      }
    }
    stream->Finish(std::move(s));
  }

  /// Notifies the application that an explicit StartSendInitialMetadata
  /// operation completed. Not used when the sending of initial metadata
  /// piggybacks onto the first write.
  ///
  /// \param[in] ok Was it successful? If false, no further write-side operation
  ///               will succeed.
  virtual void OnSendInitialMetadataDone(bool /*ok*/) {}

  /// Notifies the application that a StartRead operation completed.
  ///
  /// \param[in] ok Was it successful? If false, no further read-side operation
  ///               will succeed.
  virtual void OnReadDone(bool /*ok*/) {}

  /// Notifies the application that a StartWrite (or StartWriteLast) operation
  /// completed.
  ///
  /// \param[in] ok Was it successful? If false, no further write-side operation
  ///               will succeed.
  virtual void OnWriteDone(bool /*ok*/) {}

  /// Notifies the application that all operations associated with this RPC
  /// have completed. This is an override (from the internal base class) but
  /// still abstract, so derived classes MUST override it to be instantiated.
  void OnDone() override = 0;

  /// Notifies the application that this RPC has been cancelled. This is an
  /// override (from the internal base class) but not final, so derived classes
  /// should override it if they want to take action.
  void OnCancel() override {}

 private:
  friend class ServerCallbackReaderWriter<Request, Response>;
  // May be overridden by internal implementation details. This is not a public
  // customization point.
  virtual void InternalBindStream(
      ServerCallbackReaderWriter<Request, Response>* stream) {
    grpc::internal::MutexLock l(&stream_mu_);
    stream_.store(stream, std::memory_order_release);
    if (send_initial_metadata_wanted_) {
      stream->SendInitialMetadata();
    }
    if (read_wanted_ != nullptr) {
      stream->Read(read_wanted_);
    }
    if (write_and_finish_wanted_) {
      stream->WriteAndFinish(write_wanted_, std::move(write_options_wanted_),
                             std::move(status_wanted_));
    } else {
      if (write_wanted_ != nullptr) {
        stream->Write(write_wanted_, std::move(write_options_wanted_));
      }
      if (finish_wanted_) {
        stream->Finish(std::move(status_wanted_));
      }
    }
  }

  grpc::internal::Mutex stream_mu_;
  std::atomic<ServerCallbackReaderWriter<Request, Response>*> stream_;
  bool send_initial_metadata_wanted_ = false;
  bool write_and_finish_wanted_ = false;
  bool finish_wanted_ = false;
  Request* read_wanted_ = nullptr;
  const Response* write_wanted_ = nullptr;
  ::grpc::WriteOptions write_options_wanted_;
  ::grpc::Status status_wanted_;
};

/// \a ServerReadReactor is the interface for a client-streaming RPC.
template <class Request>
class ServerReadReactor : public internal::ServerReactor {
 public:
  ServerReadReactor() : reader_(nullptr) {}
  ~ServerReadReactor() = default;

  /// The following operation initiations are exactly like ServerBidiReactor.
  void StartSendInitialMetadata() {
    auto* reader = reader_.load(std::memory_order_acquire);
    if (reader == nullptr) {
      grpc::internal::MutexLock l(&reader_mu_);
      reader = reader_.load(std::memory_order_acquire);
      if (reader == nullptr) {
        send_initial_metadata_wanted_ = true;
        return;
      }
    }
    reader->SendInitialMetadata();
  }
  void StartRead(Request* req) {
    auto* reader = reader_.load(std::memory_order_acquire);
    if (reader == nullptr) {
      grpc::internal::MutexLock l(&reader_mu_);
      reader = reader_.load(std::memory_order_acquire);
      if (reader == nullptr) {
        read_wanted_ = req;
        return;
      }
    }
    reader->Read(req);
  }
  void Finish(::grpc::Status s) {
    auto* reader = reader_.load(std::memory_order_acquire);
    if (reader == nullptr) {
      grpc::internal::MutexLock l(&reader_mu_);
      reader = reader_.load(std::memory_order_acquire);
      if (reader == nullptr) {
        finish_wanted_ = true;
        status_wanted_ = std::move(s);
        return;
      }
    }
    reader->Finish(std::move(s));
  }

  /// The following notifications are exactly like ServerBidiReactor.
  virtual void OnSendInitialMetadataDone(bool /*ok*/) {}
  virtual void OnReadDone(bool /*ok*/) {}
  void OnDone() override = 0;
  void OnCancel() override {}

 private:
  friend class ServerCallbackReader<Request>;

  // May be overridden by internal implementation details. This is not a public
  // customization point.
  virtual void InternalBindReader(ServerCallbackReader<Request>* reader) {
    grpc::internal::MutexLock l(&reader_mu_);
    reader_.store(reader, std::memory_order_release);
    if (send_initial_metadata_wanted_) {
      reader->SendInitialMetadata();
    }
    if (read_wanted_ != nullptr) {
      reader->Read(read_wanted_);
    }
    if (finish_wanted_) {
      reader->Finish(std::move(status_wanted_));
    }
  }

  grpc::internal::Mutex reader_mu_;
  std::atomic<ServerCallbackReader<Request>*> reader_;
  bool send_initial_metadata_wanted_ = false;
  bool finish_wanted_ = false;
  Request* read_wanted_ = nullptr;
  ::grpc::Status status_wanted_;
};

/// \a ServerWriteReactor is the interface for a server-streaming RPC.
template <class Response>
class ServerWriteReactor : public internal::ServerReactor {
 public:
  ServerWriteReactor() : writer_(nullptr) {}
  ~ServerWriteReactor() = default;

  /// The following operation initiations are exactly like ServerBidiReactor.
  void StartSendInitialMetadata() {
    auto* writer = writer_.load(std::memory_order_acquire);
    if (writer == nullptr) {
      grpc::internal::MutexLock l(&writer_mu_);
      writer = writer_.load(std::memory_order_acquire);
      if (writer == nullptr) {
        send_initial_metadata_wanted_ = true;
        return;
      }
    }
    writer->SendInitialMetadata();
  }
  void StartWrite(const Response* resp) {
    StartWrite(resp, ::grpc::WriteOptions());
  }
  void StartWrite(const Response* resp, ::grpc::WriteOptions options) {
    auto* writer = writer_.load(std::memory_order_acquire);
    if (writer == nullptr) {
      grpc::internal::MutexLock l(&writer_mu_);
      writer = writer_.load(std::memory_order_acquire);
      if (writer == nullptr) {
        write_wanted_ = resp;
        write_options_wanted_ = std::move(options);
        return;
      }
    }
    writer->Write(resp, std::move(options));
  }
  void StartWriteAndFinish(const Response* resp, ::grpc::WriteOptions options,
                           ::grpc::Status s) {
    auto* writer = writer_.load(std::memory_order_acquire);
    if (writer == nullptr) {
      grpc::internal::MutexLock l(&writer_mu_);
      writer = writer_.load(std::memory_order_acquire);
      if (writer == nullptr) {
        write_and_finish_wanted_ = true;
        write_wanted_ = resp;
        write_options_wanted_ = std::move(options);
        status_wanted_ = std::move(s);
        return;
      }
    }
    writer->WriteAndFinish(resp, std::move(options), std::move(s));
  }
  void StartWriteLast(const Response* resp, ::grpc::WriteOptions options) {
    StartWrite(resp, std::move(options.set_last_message()));
  }
  void Finish(::grpc::Status s) {
    auto* writer = writer_.load(std::memory_order_acquire);
    if (writer == nullptr) {
      grpc::internal::MutexLock l(&writer_mu_);
      writer = writer_.load(std::memory_order_acquire);
      if (writer == nullptr) {
        finish_wanted_ = true;
        status_wanted_ = std::move(s);
        return;
      }
    }
    writer->Finish(std::move(s));
  }

  /// The following notifications are exactly like ServerBidiReactor.
  virtual void OnSendInitialMetadataDone(bool /*ok*/) {}
  virtual void OnWriteDone(bool /*ok*/) {}
  void OnDone() override = 0;
  void OnCancel() override {}

 private:
  friend class ServerCallbackWriter<Response>;
  // May be overridden by internal implementation details. This is not a public
  // customization point.
  virtual void InternalBindWriter(ServerCallbackWriter<Response>* writer) {
    grpc::internal::MutexLock l(&writer_mu_);
    writer_.store(writer, std::memory_order_release);
    if (send_initial_metadata_wanted_) {
      writer->SendInitialMetadata();
    }
    if (write_and_finish_wanted_) {
      writer->WriteAndFinish(write_wanted_, std::move(write_options_wanted_),
                             std::move(status_wanted_));
    } else {
      if (write_wanted_ != nullptr) {
        writer->Write(write_wanted_, std::move(write_options_wanted_));
      }
      if (finish_wanted_) {
        writer->Finish(std::move(status_wanted_));
      }
    }
  }

  grpc::internal::Mutex writer_mu_;
  std::atomic<ServerCallbackWriter<Response>*> writer_;
  bool send_initial_metadata_wanted_ = false;
  bool write_and_finish_wanted_ = false;
  bool finish_wanted_ = false;
  const Response* write_wanted_ = nullptr;
  ::grpc::WriteOptions write_options_wanted_;
  ::grpc::Status status_wanted_;
};

class ServerUnaryReactor : public internal::ServerReactor {
 public:
  ServerUnaryReactor() : call_(nullptr) {}
  ~ServerUnaryReactor() = default;

  /// The following operation initiations are exactly like ServerBidiReactor.
  void StartSendInitialMetadata() {
    auto* call = call_.load(std::memory_order_acquire);
    if (call == nullptr) {
      grpc::internal::MutexLock l(&call_mu_);
      call = call_.load(std::memory_order_acquire);
      if (call == nullptr) {
        send_initial_metadata_wanted_ = true;
        return;
      }
    }
    call->SendInitialMetadata();
  }
  void Finish(::grpc::Status s) {
    auto* call = call_.load(std::memory_order_acquire);
    if (call == nullptr) {
      grpc::internal::MutexLock l(&call_mu_);
      call = call_.load(std::memory_order_acquire);
      if (call == nullptr) {
        finish_wanted_ = true;
        status_wanted_ = std::move(s);
        return;
      }
    }
    call->Finish(std::move(s));
  }

  /// The following notifications are exactly like ServerBidiReactor.
  virtual void OnSendInitialMetadataDone(bool /*ok*/) {}
  void OnDone() override = 0;
  void OnCancel() override {}

 private:
  friend class ServerCallbackUnary;
  // May be overridden by internal implementation details. This is not a public
  // customization point.
  virtual void InternalBindCall(ServerCallbackUnary* call) {
    grpc::internal::MutexLock l(&call_mu_);
    call_.store(call, std::memory_order_release);
    if (send_initial_metadata_wanted_) {
      call->SendInitialMetadata();
    }
    if (finish_wanted_) {
      call->Finish(std::move(status_wanted_));
    }
  }

  grpc::internal::Mutex call_mu_;
  std::atomic<ServerCallbackUnary*> call_;
  bool send_initial_metadata_wanted_ = false;
  bool finish_wanted_ = false;
  ::grpc::Status status_wanted_;
};

}  // namespace experimental

namespace internal {

template <class Base>
class FinishOnlyReactor : public Base {
 public:
  explicit FinishOnlyReactor(::grpc::Status s) { this->Finish(std::move(s)); }
  void OnDone() override { this->~FinishOnlyReactor(); }
};

using UnimplementedUnaryReactor =
    FinishOnlyReactor<experimental::ServerUnaryReactor>;
template <class Request>
using UnimplementedReadReactor =
    FinishOnlyReactor<experimental::ServerReadReactor<Request>>;
template <class Response>
using UnimplementedWriteReactor =
    FinishOnlyReactor<experimental::ServerWriteReactor<Response>>;
template <class Request, class Response>
using UnimplementedBidiReactor =
    FinishOnlyReactor<experimental::ServerBidiReactor<Request, Response>>;

}  // namespace internal
}  // namespace grpc_impl

#endif  // GRPCPP_IMPL_CODEGEN_SERVER_CALLBACK_IMPL_H
