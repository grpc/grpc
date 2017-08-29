/*
 *
 * Copyright 2015-2016 gRPC authors.
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
 *
 */

/// A completion queue implements a concurrent producer-consumer queue, with
/// two main API-exposed methods: \a Next and \a AsyncNext. These
/// methods are the essential component of the gRPC C++ asynchronous API.
/// There is also a \a Shutdown method to indicate that a given completion queue
/// will no longer have regular events. This must be called before the
/// completion queue is destroyed.
/// All completion queue APIs are thread-safe and may be used concurrently with
/// any other completion queue API invocation; it is acceptable to have
/// multiple threads calling \a Next or \a AsyncNext on the same or different
/// completion queues, or to call these methods concurrently with a \a Shutdown
/// elsewhere.
/// \remark{All other API calls on completion queue should be completed before
/// a completion queue destructor is called.}
#ifndef GRPCXX_IMPL_CODEGEN_COMPLETION_QUEUE_H
#define GRPCXX_IMPL_CODEGEN_COMPLETION_QUEUE_H

#include <grpc++/impl/codegen/completion_queue_tag.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/grpc_library.h>
#include <grpc++/impl/codegen/status.h>
#include <grpc++/impl/codegen/time.h>
#include <grpc/impl/codegen/atm.h>

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
namespace internal {
template <class W, class R>
class ServerReaderWriterBody;
}
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

/// A thin wrapper around \ref grpc_completion_queue (see \ref
/// src/core/lib/surface/completion_queue.h).
/// See \ref doc/cpp/perf_notes.md for notes on best practices for high
/// performance servers.
class CompletionQueue : private GrpcLibraryCodegen {
 public:
  /// Default constructor. Implicitly creates a \a grpc_completion_queue
  /// instance.
  CompletionQueue()
      : CompletionQueue(grpc_completion_queue_attributes{
            GRPC_CQ_CURRENT_VERSION, GRPC_CQ_NEXT, GRPC_CQ_DEFAULT_POLLING}) {}

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
  /// \warning This method must be called at some point if this completion queue
  /// is accessed with Next or AsyncNext. Once invoked, \a Next
  /// will start to return false and \a AsyncNext will return \a
  /// NextStatus::SHUTDOWN. Only once either one of these methods does that
  /// (that is, once the queue has been \em drained) can an instance of this
  /// class be destroyed. Also note that applications must ensure that
  /// no work is enqueued on this completion queue after this method is called.
  void Shutdown();

  /// Returns a \em raw pointer to the underlying \a grpc_completion_queue
  /// instance.
  ///
  /// \warning Remember that the returned instance is owned. No transfer of
  /// owership is performed.
  grpc_completion_queue* cq() { return cq_; }

  /// Manage state of avalanching operations : completion queue tags that
  /// trigger other completion queue operations. The underlying core completion
  /// queue should not really shutdown until all avalanching operations have
  /// been finalized. Note that we maintain the requirement that an avalanche
  /// registration must take place before CQ shutdown (which must be maintained
  /// elsehwere)
  void InitialAvalanching() {
    gpr_atm_rel_store(&avalanches_in_flight_, static_cast<gpr_atm>(1));
  }
  void RegisterAvalanching() {
    gpr_atm_no_barrier_fetch_add(&avalanches_in_flight_,
                                 static_cast<gpr_atm>(1));
  }
  void CompleteAvalanching();

 protected:
  /// Private constructor of CompletionQueue only visible to friend classes
  CompletionQueue(const grpc_completion_queue_attributes& attributes) {
    cq_ = g_core_codegen_interface->grpc_completion_queue_create(
        g_core_codegen_interface->grpc_completion_queue_factory_lookup(
            &attributes),
        &attributes, NULL);
    InitialAvalanching();  // reserve this for the future shutdown
  }

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
  friend class ::grpc::internal::ServerReaderWriterBody;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class RpcMethodHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class ClientStreamingHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class ServerStreamingHandler;
  template <class Streamer, bool WriteNeeded>
  friend class TemplatedBidiStreamingHandler;
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
  ///
  /// TODO: sreek - This calls tag->FinalizeResult() even if the cq_ is already
  /// shutdown. This is most likely a bug and if it is a bug, then change this
  /// implementation to simple call the other TryPluck function with a zero
  /// timeout. i.e:
  ///      TryPluck(tag, gpr_time_0(GPR_CLOCK_REALTIME))
  void TryPluck(CompletionQueueTag* tag) {
    auto deadline = g_core_codegen_interface->gpr_time_0(GPR_CLOCK_REALTIME);
    auto ev = g_core_codegen_interface->grpc_completion_queue_pluck(
        cq_, tag, deadline, nullptr);
    if (ev.type == GRPC_QUEUE_TIMEOUT) return;
    bool ok = ev.success != 0;
    void* ignored = tag;
    // the tag must be swallowed if using TryPluck
    GPR_CODEGEN_ASSERT(!tag->FinalizeResult(&ignored, &ok));
  }

  /// Performs a single polling pluck on \a tag. Calls tag->FinalizeResult if
  /// the pluck() was successful and returned the tag.
  ///
  /// This exects tag->FinalizeResult (if called) to return 'false' i.e expects
  /// that the tag is internal not something that is returned to the user.
  void TryPluck(CompletionQueueTag* tag, gpr_timespec deadline) {
    auto ev = g_core_codegen_interface->grpc_completion_queue_pluck(
        cq_, tag, deadline, nullptr);
    if (ev.type == GRPC_QUEUE_TIMEOUT || ev.type == GRPC_QUEUE_SHUTDOWN) {
      return;
    }

    bool ok = ev.success != 0;
    void* ignored = tag;
    GPR_CODEGEN_ASSERT(!tag->FinalizeResult(&ignored, &ok));
  }

  grpc_completion_queue* cq_;  // owned

  gpr_atm avalanches_in_flight_;
};

/// A specific type of completion queue used by the processing of notifications
/// by servers. Instantiated by \a ServerBuilder.
class ServerCompletionQueue : public CompletionQueue {
 public:
  bool IsFrequentlyPolled() { return polling_type_ != GRPC_CQ_NON_LISTENING; }

 private:
  grpc_cq_polling_type polling_type_;
  friend class ServerBuilder;
  /// \param is_frequently_polled Informs the GRPC library about whether the
  /// server completion queue would be actively polled (by calling Next() or
  /// AsyncNext()). By default all server completion queues are assumed to be
  /// frequently polled.
  ServerCompletionQueue(grpc_cq_polling_type polling_type)
      : CompletionQueue(grpc_completion_queue_attributes{
            GRPC_CQ_CURRENT_VERSION, GRPC_CQ_NEXT, polling_type}),
        polling_type_(polling_type) {}
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_COMPLETION_QUEUE_H
