/*
 *
 * Copyright 2015-2016, Google Inc.
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

/// A completion queue implements a concurrent producer-consumer queue, with two
/// main methods, \a Next and \a AsyncNext.
#ifndef GRPCXX_IMPL_CODEGEN_COMPLETION_QUEUE_H
#define GRPCXX_IMPL_CODEGEN_COMPLETION_QUEUE_H

#include <grpc++/impl/codegen/completion_queue_tag.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/grpc_library.h>
#include <grpc++/impl/codegen/status.h>
#include <grpc++/impl/codegen/time.h>
#include <grpc/impl/codegen/time.h>

struct grpc_completion_queue;

namespace grpc {

template <class R>
class ClientReader;
template <class W>
class ClientWriter;
template <class W, class R>
class ClientReaderWriter;
template <class R>
class ServerReader;
template <class W>
class ServerWriter;
template <class W, class R>
class ServerReaderWriter;
template <class ServiceType, class RequestType, class ResponseType>
class RpcMethodHandler;
template <class ServiceType, class RequestType, class ResponseType>
class ClientStreamingHandler;
template <class ServiceType, class RequestType, class ResponseType>
class ServerStreamingHandler;
template <class ServiceType, class RequestType, class ResponseType>
class BidiStreamingHandler;
class UnknownMethodHandler;

class Channel;
class ChannelInterface;
class ClientContext;
class CompletionQueueTag;
class CompletionQueue;
class RpcMethod;
class Server;
class ServerBuilder;
class ServerContext;

extern CoreCodegenInterface* g_core_codegen_interface;

/// A thin wrapper around \a grpc_completion_queue (see / \a
/// src/core/surface/completion_queue.h).
class CompletionQueue : private GrpcLibraryCodegen {
 public:
  /// Default constructor. Implicitly creates a \a grpc_completion_queue
  /// instance.
  CompletionQueue() {
    cq_ = g_core_codegen_interface->grpc_completion_queue_create(nullptr);
  }

  /// Wrap \a take, taking ownership of the instance.
  ///
  /// \param take The completion queue instance to wrap. Ownership is taken.
  explicit CompletionQueue(grpc_completion_queue* take);

  /// Destructor. Destroys the owned wrapped completion queue / instance.
  ~CompletionQueue() {
    g_core_codegen_interface->grpc_completion_queue_destroy(cq_);
  }

  /// Tri-state return for AsyncNext: SHUTDOWN, GOT_EVENT, TIMEOUT.
  enum NextStatus {
    SHUTDOWN,   ///< The completion queue has been shutdown.
    GOT_EVENT,  ///< Got a new event; \a tag will be filled in with its
                ///< associated value; \a ok indicating its success.
    TIMEOUT     ///< deadline was reached.
  };

  /// Read from the queue, blocking up to \a deadline (or the queue's shutdown).
  /// Both \a tag and \a ok are updated upon success (if an event is available
  /// within the \a deadline).  A \a tag points to an arbitrary location usually
  /// employed to uniquely identify an event.
  ///
  /// \param tag[out] Upon sucess, updated to point to the event's tag.
  /// \param ok[out] Upon sucess, true if read a regular event, false otherwise.
  /// \param deadline[in] How long to block in wait for an event.
  ///
  /// \return The type of event read.
  template <typename T>
  NextStatus AsyncNext(void** tag, bool* ok, const T& deadline) {
    TimePoint<T> deadline_tp(deadline);
    return AsyncNextInternal(tag, ok, deadline_tp.raw_time());
  }

  /// Read from the queue, blocking until an event is available or the queue is
  /// shutting down.
  ///
  /// \param tag[out] Updated to point to the read event's tag.
  /// \param ok[out] true if read a regular event, false otherwise.
  ///
  /// \return true if read a regular event, false if the queue is shutting down.
  bool Next(void** tag, bool* ok) {
    return (AsyncNextInternal(tag, ok, g_core_codegen_interface->gpr_inf_future(
                                           GPR_CLOCK_REALTIME)) != SHUTDOWN);
  }

  /// Request the shutdown of the queue.
  ///
  /// \warning This method must be called at some point. Once invoked, \a Next
  /// will start to return false and \a AsyncNext will return \a
  /// NextStatus::SHUTDOWN. Only once either one of these methods does that
  /// (that is, once the queue has been \em drained) can an instance of this
  /// class be destroyed.
  void Shutdown();

  /// Returns a \em raw pointer to the underlying \a grpc_completion_queue
  /// instance.
  ///
  /// \warning Remember that the returned instance is owned. No transfer of
  /// owership is performed.
  grpc_completion_queue* cq() { return cq_; }

 private:
  // Friend synchronous wrappers so that they can access Pluck(), which is
  // a semi-private API geared towards the synchronous implementation.
  template <class R>
  friend class ::grpc::ClientReader;
  template <class W>
  friend class ::grpc::ClientWriter;
  template <class W, class R>
  friend class ::grpc::ClientReaderWriter;
  template <class R>
  friend class ::grpc::ServerReader;
  template <class W>
  friend class ::grpc::ServerWriter;
  template <class W, class R>
  friend class ::grpc::ServerReaderWriter;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class RpcMethodHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class ClientStreamingHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class ServerStreamingHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class BidiStreamingHandler;
  friend class UnknownMethodHandler;
  friend class ::grpc::Server;
  friend class ::grpc::ServerContext;
  template <class InputMessage, class OutputMessage>
  friend Status BlockingUnaryCall(ChannelInterface* channel,
                                  const RpcMethod& method,
                                  ClientContext* context,
                                  const InputMessage& request,
                                  OutputMessage* result);

  NextStatus AsyncNextInternal(void** tag, bool* ok, gpr_timespec deadline);

  /// Wraps \a grpc_completion_queue_pluck.
  /// \warning Must not be mixed with calls to \a Next.
  bool Pluck(CompletionQueueTag* tag) {
    auto deadline =
        g_core_codegen_interface->gpr_inf_future(GPR_CLOCK_REALTIME);
    auto ev = g_core_codegen_interface->grpc_completion_queue_pluck(
        cq_, tag, deadline, nullptr);
    bool ok = ev.success != 0;
    void* ignored = tag;
    GPR_CODEGEN_ASSERT(tag->FinalizeResult(&ignored, &ok));
    GPR_CODEGEN_ASSERT(ignored == tag);
    // Ignore mutations by FinalizeResult: Pluck returns the C API status
    return ev.success != 0;
  }

  /// Performs a single polling pluck on \a tag.
  /// \warning Must not be mixed with calls to \a Next.
  void TryPluck(CompletionQueueTag* tag) {
    auto deadline = gpr_time_0(GPR_CLOCK_REALTIME);
    auto ev = g_core_codegen_interface->grpc_completion_queue_pluck(
        cq_, tag, deadline, nullptr);
    if (ev.type == GRPC_QUEUE_TIMEOUT) return;
    bool ok = ev.success != 0;
    void* ignored = tag;
    // the tag must be swallowed if using TryPluck
    GPR_CODEGEN_ASSERT(!tag->FinalizeResult(&ignored, &ok));
  }

  grpc_completion_queue* cq_;  // owned
};

/// A specific type of completion queue used by the processing of notifications
/// by servers. Instantiated by \a ServerBuilder.
class ServerCompletionQueue : public CompletionQueue {
 public:
  bool IsFrequentlyPolled() { return is_frequently_polled_; }

 private:
  bool is_frequently_polled_;
  friend class ServerBuilder;
  /// \param is_frequently_polled Informs the GPRC library about whether the
  /// server completion queue would be actively polled (by calling Next() or
  /// AsyncNext()). By default all server completion queues are assumed to be
  /// frequently polled.
  ServerCompletionQueue(bool is_frequently_polled = true)
      : is_frequently_polled_(is_frequently_polled) {}
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_COMPLETION_QUEUE_H
