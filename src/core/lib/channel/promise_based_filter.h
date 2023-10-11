// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GRPC_SRC_CORE_LIB_CHANNEL_PROMISE_BASED_FILTER_H
#define GRPC_SRC_CORE_LIB_CHANNEL_PROMISE_BASED_FILTER_H

// Scaffolding to allow the per-call part of a filter to be authored in a
// promise-style. Most of this will be removed once the promises conversion is
// completed.

#include <grpc/support/port_platform.h>

#include <stdint.h>
#include <stdlib.h>

#include <atomic>
#include <initializer_list>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/functional/function_ref.h"
#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/call_finalization.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/event_engine/default_event_engine.h"  // IWYU pragma: keep
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

class ChannelFilter {
 public:
  class Args {
   public:
    Args() : Args(nullptr, nullptr) {}
    explicit Args(grpc_channel_stack* channel_stack,
                  grpc_channel_element* channel_element)
        : channel_stack_(channel_stack), channel_element_(channel_element) {}

    grpc_channel_stack* channel_stack() const { return channel_stack_; }
    grpc_channel_element* uninitialized_channel_element() {
      return channel_element_;
    }

   private:
    friend class ChannelFilter;
    grpc_channel_stack* channel_stack_;
    grpc_channel_element* channel_element_;
  };

  // Perform post-initialization step (if any).
  virtual void PostInit() {}

  // Construct a promise for one call.
  virtual ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) = 0;

  // Start a legacy transport op
  // Return true if the op was handled, false if it should be passed to the
  // next filter.
  // TODO(ctiller): design a new API for this - we probably don't want big op
  // structures going forward.
  virtual bool StartTransportOp(grpc_transport_op*) { return false; }

  // Perform a legacy get info call
  // Return true if the op was handled, false if it should be passed to the
  // next filter.
  // TODO(ctiller): design a new API for this
  virtual bool GetChannelInfo(const grpc_channel_info*) { return false; }

  virtual ~ChannelFilter() = default;

  grpc_event_engine::experimental::EventEngine*
  hack_until_per_channel_stack_event_engines_land_get_event_engine() {
    return event_engine_.get();
  }

 private:
  // TODO(ctiller): remove once per-channel-stack EventEngines land
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
};

// Designator for whether a filter is client side or server side.
// Please don't use this outside calls to MakePromiseBasedFilter - it's
// intended to be deleted once the promise conversion is complete.
enum class FilterEndpoint {
  kClient,
  kServer,
};

// Flags for MakePromiseBasedFilter.
static constexpr uint8_t kFilterExaminesServerInitialMetadata = 1;
static constexpr uint8_t kFilterIsLast = 2;
static constexpr uint8_t kFilterExaminesOutboundMessages = 4;
static constexpr uint8_t kFilterExaminesInboundMessages = 8;
static constexpr uint8_t kFilterExaminesCallContext = 16;

namespace promise_filter_detail {

// Proxy channel filter for initialization failure, since we must leave a
// valid filter in place.
class InvalidChannelFilter : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs, NextPromiseFactory) override {
    abort();
  }
};

// Call data shared between all implementations of promise-based filters.
class BaseCallData : public Activity, private Wakeable {
 protected:
  // Hook to allow interception of messages on the send/receive path by
  // PipeSender and PipeReceiver, as appropriate according to whether we're
  // client or server.
  class Interceptor {
   public:
    virtual PipeSender<MessageHandle>* Push() = 0;
    virtual PipeReceiver<MessageHandle>* Pull() = 0;
    virtual PipeReceiver<MessageHandle>* original_receiver() = 0;
    virtual PipeSender<MessageHandle>* original_sender() = 0;
    virtual void GotPipe(PipeSender<MessageHandle>*) = 0;
    virtual void GotPipe(PipeReceiver<MessageHandle>*) = 0;
    virtual ~Interceptor() = default;
  };

  BaseCallData(grpc_call_element* elem, const grpc_call_element_args* args,
               uint8_t flags,
               absl::FunctionRef<Interceptor*()> make_send_interceptor,
               absl::FunctionRef<Interceptor*()> make_recv_interceptor);

 public:
  ~BaseCallData() override;

  void set_pollent(grpc_polling_entity* pollent) {
    GPR_ASSERT(nullptr ==
               pollent_.exchange(pollent, std::memory_order_release));
  }

  // Activity implementation (partial).
  void Orphan() final;
  Waker MakeNonOwningWaker() final;
  Waker MakeOwningWaker() final;

  std::string ActivityDebugTag(WakeupMask) const override { return DebugTag(); }

  void Finalize(const grpc_call_final_info* final_info) {
    ScopedContext ctx(this);
    finalization_.Run(final_info);
  }

  virtual void StartBatch(grpc_transport_stream_op_batch* batch) = 0;

 protected:
  class ScopedContext
      : public promise_detail::Context<Arena>,
        public promise_detail::Context<grpc_call_context_element>,
        public promise_detail::Context<grpc_polling_entity>,
        public promise_detail::Context<CallFinalization>,
        public promise_detail::Context<
            grpc_event_engine::experimental::EventEngine>,
        public promise_detail::Context<CallContext> {
   public:
    explicit ScopedContext(BaseCallData* call_data)
        : promise_detail::Context<Arena>(call_data->arena_),
          promise_detail::Context<grpc_call_context_element>(
              call_data->context_),
          promise_detail::Context<grpc_polling_entity>(
              call_data->pollent_.load(std::memory_order_acquire)),
          promise_detail::Context<CallFinalization>(&call_data->finalization_),
          promise_detail::Context<grpc_event_engine::experimental::EventEngine>(
              call_data->event_engine_),
          promise_detail::Context<CallContext>(call_data->call_context_) {}
  };

  class Flusher {
   public:
    explicit Flusher(BaseCallData* call);
    // Calls closures, schedules batches, relinquishes call combiner.
    ~Flusher();

    void Resume(grpc_transport_stream_op_batch* batch) {
      GPR_ASSERT(!call_->is_last());
      if (batch->HasOp()) {
        release_.push_back(batch);
      } else if (batch->on_complete != nullptr) {
        Complete(batch);
      }
    }

    void Cancel(grpc_transport_stream_op_batch* batch,
                grpc_error_handle error) {
      grpc_transport_stream_op_batch_queue_finish_with_failure(batch, error,
                                                               &call_closures_);
    }

    void Complete(grpc_transport_stream_op_batch* batch) {
      call_closures_.Add(batch->on_complete, absl::OkStatus(),
                         "Flusher::Complete");
    }

    void AddClosure(grpc_closure* closure, grpc_error_handle error,
                    const char* reason) {
      call_closures_.Add(closure, error, reason);
    }

    BaseCallData* call() const { return call_; }

   private:
    absl::InlinedVector<grpc_transport_stream_op_batch*, 1> release_;
    CallCombinerClosureList call_closures_;
    BaseCallData* const call_;
  };

  // Smart pointer like wrapper around a batch.
  // Creation makes a ref count of one capture.
  // Copying increments.
  // Must be moved from or resumed or cancelled before destruction.
  class CapturedBatch final {
   public:
    CapturedBatch();
    explicit CapturedBatch(grpc_transport_stream_op_batch* batch);
    ~CapturedBatch();
    CapturedBatch(const CapturedBatch&);
    CapturedBatch& operator=(const CapturedBatch&);
    CapturedBatch(CapturedBatch&&) noexcept;
    CapturedBatch& operator=(CapturedBatch&&) noexcept;

    grpc_transport_stream_op_batch* operator->() { return batch_; }
    bool is_captured() const { return batch_ != nullptr; }

    // Resume processing this batch (releases one ref, passes it down the
    // stack)
    void ResumeWith(Flusher* releaser);
    // Cancel this batch immediately (releases all refs)
    void CancelWith(grpc_error_handle error, Flusher* releaser);
    // Complete this batch (pass it up) assuming refs drop to zero
    void CompleteWith(Flusher* releaser);

    void Swap(CapturedBatch* other) { std::swap(batch_, other->batch_); }

   private:
    grpc_transport_stream_op_batch* batch_;
  };

  static Arena::PoolPtr<grpc_metadata_batch> WrapMetadata(
      grpc_metadata_batch* p) {
    return Arena::PoolPtr<grpc_metadata_batch>(p,
                                               Arena::PooledDeleter(nullptr));
  }

  class ReceiveInterceptor final : public Interceptor {
   public:
    explicit ReceiveInterceptor(Arena* arena) : pipe_{arena} {}

    PipeReceiver<MessageHandle>* original_receiver() override {
      return &pipe_.receiver;
    }
    PipeSender<MessageHandle>* original_sender() override { abort(); }

    void GotPipe(PipeReceiver<MessageHandle>* receiver) override {
      GPR_ASSERT(receiver_ == nullptr);
      receiver_ = receiver;
    }

    void GotPipe(PipeSender<MessageHandle>*) override { abort(); }

    PipeSender<MessageHandle>* Push() override { return &pipe_.sender; }
    PipeReceiver<MessageHandle>* Pull() override {
      GPR_ASSERT(receiver_ != nullptr);
      return receiver_;
    }

   private:
    Pipe<MessageHandle> pipe_;
    PipeReceiver<MessageHandle>* receiver_ = nullptr;
  };

  class SendInterceptor final : public Interceptor {
   public:
    explicit SendInterceptor(Arena* arena) : pipe_{arena} {}

    PipeReceiver<MessageHandle>* original_receiver() override { abort(); }
    PipeSender<MessageHandle>* original_sender() override {
      return &pipe_.sender;
    }

    void GotPipe(PipeReceiver<MessageHandle>*) override { abort(); }

    void GotPipe(PipeSender<MessageHandle>* sender) override {
      GPR_ASSERT(sender_ == nullptr);
      sender_ = sender;
    }

    PipeSender<MessageHandle>* Push() override {
      GPR_ASSERT(sender_ != nullptr);
      return sender_;
    }
    PipeReceiver<MessageHandle>* Pull() override { return &pipe_.receiver; }

   private:
    Pipe<MessageHandle> pipe_;
    PipeSender<MessageHandle>* sender_ = nullptr;
  };

  // State machine for sending messages: handles intercepting send_message ops
  // and forwarding them through pipes to the promise, then getting the result
  // down the stack.
  // Split into its own class so that we don't spend the memory instantiating
  // these members for filters that don't need to intercept sent messages.
  class SendMessage {
   public:
    SendMessage(BaseCallData* base, Interceptor* interceptor)
        : base_(base), interceptor_(interceptor) {}
    ~SendMessage() { interceptor_->~Interceptor(); }

    Interceptor* interceptor() { return interceptor_; }

    // Start a send_message op.
    void StartOp(CapturedBatch batch);
    // Publish the outbound pipe to the filter.
    // This happens when the promise requests to call the next filter: until
    // this occurs messages can't be sent as we don't know the pipe that the
    // promise expects to send on.
    template <typename T>
    void GotPipe(T* pipe);
    // Called from client/server polling to do the send message part of the
    // work.
    void WakeInsideCombiner(Flusher* flusher, bool allow_push_to_pipe);
    // Call is completed, we have trailing metadata. Close things out.
    void Done(const ServerMetadata& metadata, Flusher* flusher);
    // Return true if we have a batch captured (for debug logs)
    bool HaveCapturedBatch() const { return batch_.is_captured(); }
    // Return true if we're not actively sending a message.
    bool IsIdle() const;
    // Return true if we've released the message for forwarding down the stack.
    bool IsForwarded() const { return state_ == State::kForwardedBatch; }

   private:
    enum class State : uint8_t {
      // Starting state: no batch started, no outgoing pipe configured.
      kInitial,
      // We have an outgoing pipe, but no batch started.
      // (this is the steady state).
      kIdle,
      // We have a batch started, but no outgoing pipe configured.
      // Stall until we have one.
      kGotBatchNoPipe,
      // We have a batch, and an outgoing pipe. On the next poll we'll push the
      // message into the pipe to the promise.
      kGotBatch,
      // We've pushed a message into the promise, and we're now waiting for it
      // to pop out the other end so we can forward it down the stack.
      kPushedToPipe,
      // We've forwarded a message down the stack, and now we're waiting for
      // completion.
      kForwardedBatch,
      // We've got the completion callback, we'll close things out during poll
      // and then forward completion callbacks up and transition back to idle.
      kBatchCompleted,
      // We're almost done, but need to poll first.
      kCancelledButNotYetPolled,
      // We're done.
      kCancelled,
      // We're done, but we haven't gotten a status yet
      kCancelledButNoStatus,
    };
    static const char* StateString(State);

    void OnComplete(absl::Status status);

    BaseCallData* const base_;
    State state_ = State::kInitial;
    Interceptor* const interceptor_;
    absl::optional<PipeSender<MessageHandle>::PushType> push_;
    absl::optional<PipeReceiverNextType<MessageHandle>> next_;
    CapturedBatch batch_;
    grpc_closure* intercepted_on_complete_;
    grpc_closure on_complete_ =
        MakeMemberClosure<SendMessage, &SendMessage::OnComplete>(this);
    absl::Status completed_status_;
  };

  // State machine for receiving messages: handles intercepting recv_message
  // ops, forwarding them down the stack, and then publishing the result via
  // pipes to the promise (and ultimately calling the right callbacks for the
  // batch when our promise has completed processing of them).
  // Split into its own class so that we don't spend the memory instantiating
  // these members for filters that don't need to intercept sent messages.
  class ReceiveMessage {
   public:
    ReceiveMessage(BaseCallData* base, Interceptor* interceptor)
        : base_(base), interceptor_(interceptor) {}
    ~ReceiveMessage() { interceptor_->~Interceptor(); }

    Interceptor* interceptor() { return interceptor_; }

    // Start a recv_message op.
    void StartOp(CapturedBatch& batch);
    // Publish the inbound pipe to the filter.
    // This happens when the promise requests to call the next filter: until
    // this occurs messages can't be received as we don't know the pipe that the
    // promise expects to forward them with.
    template <typename T>
    void GotPipe(T* pipe);
    // Called from client/server polling to do the receive message part of the
    // work.
    void WakeInsideCombiner(Flusher* flusher, bool allow_push_to_pipe);
    // Call is completed, we have trailing metadata. Close things out.
    void Done(const ServerMetadata& metadata, Flusher* flusher);

   private:
    enum class State : uint8_t {
      // Starting state: no batch started, no incoming pipe configured.
      kInitial,
      // We have an incoming pipe, but no batch started.
      // (this is the steady state).
      kIdle,
      // We received a batch and forwarded it on, but have not got an incoming
      // pipe configured.
      kForwardedBatchNoPipe,
      // We received a batch and forwarded it on.
      kForwardedBatch,
      // We got the completion for the recv_message, but we don't yet have a
      // pipe configured. Stall until this changes.
      kBatchCompletedNoPipe,
      // We got the completion for the recv_message, and we have a pipe
      // configured: next poll will push the message into the pipe for the
      // filter to process.
      kBatchCompleted,
      // We've pushed a message into the promise, and we're now waiting for it
      // to pop out the other end so we can forward it up the stack.
      kPushedToPipe,
      // We've got a message out of the pipe, now we need to wait for processing
      // to completely quiesce in the promise prior to forwarding the completion
      // up the stack.
      kPulledFromPipe,
      // We're done.
      kCancelled,
      // Call got terminated whilst we were idle: we need to close the sender
      // pipe next poll.
      kCancelledWhilstIdle,
      // Call got terminated whilst we had forwarded a recv_message down the
      // stack: we need to keep track of that until we get the completion so
      // that we do the right thing in OnComplete.
      kCancelledWhilstForwarding,
      // The same, but before we got the pipe
      kCancelledWhilstForwardingNoPipe,
      // Call got terminated whilst we had a recv_message batch completed, and
      // we've now received the completion.
      // On the next poll we'll close things out and forward on completions,
      // then transition to cancelled.
      kBatchCompletedButCancelled,
      // The same, but before we got the pipe
      kBatchCompletedButCancelledNoPipe,
      // Completed successfully while we're processing a recv message - see
      // kPushedToPipe.
      kCompletedWhilePushedToPipe,
      // Completed successfully while we're processing a recv message - see
      // kPulledFromPipe.
      kCompletedWhilePulledFromPipe,
      // Completed successfully while we were waiting to process
      // kBatchCompleted.
      kCompletedWhileBatchCompleted,
    };
    static const char* StateString(State);

    void OnComplete(absl::Status status);

    BaseCallData* const base_;
    Interceptor* const interceptor_;
    State state_ = State::kInitial;
    uint32_t scratch_flags_;
    absl::optional<SliceBuffer>* intercepted_slice_buffer_;
    uint32_t* intercepted_flags_;
    absl::optional<PipeSender<MessageHandle>::PushType> push_;
    absl::optional<PipeReceiverNextType<MessageHandle>> next_;
    absl::Status completed_status_;
    grpc_closure* intercepted_on_complete_;
    grpc_closure on_complete_ =
        MakeMemberClosure<ReceiveMessage, &ReceiveMessage::OnComplete>(this);
  };

  Arena* arena() { return arena_; }
  grpc_call_element* elem() const { return elem_; }
  CallCombiner* call_combiner() const { return call_combiner_; }
  Timestamp deadline() const { return deadline_; }
  grpc_call_stack* call_stack() const { return call_stack_; }
  Pipe<ServerMetadataHandle>* server_initial_metadata_pipe() const {
    return server_initial_metadata_pipe_;
  }
  SendMessage* send_message() const { return send_message_; }
  ReceiveMessage* receive_message() const { return receive_message_; }

  bool is_last() const {
    return grpc_call_stack_element(call_stack_, call_stack_->count - 1) ==
           elem_;
  }

  virtual void WakeInsideCombiner(Flusher* flusher) = 0;

  virtual absl::string_view ClientOrServerString() const = 0;
  std::string LogTag() const;

 private:
  // Wakeable implementation.
  void Wakeup(WakeupMask) final;
  void WakeupAsync(WakeupMask) final { Crash("not implemented"); }
  void Drop(WakeupMask) final;

  virtual void OnWakeup() = 0;

  grpc_call_stack* const call_stack_;
  grpc_call_element* const elem_;
  Arena* const arena_;
  CallCombiner* const call_combiner_;
  const Timestamp deadline_;
  CallFinalization finalization_;
  CallContext* call_context_ = nullptr;
  grpc_call_context_element* const context_;
  std::atomic<grpc_polling_entity*> pollent_{nullptr};
  Pipe<ServerMetadataHandle>* const server_initial_metadata_pipe_;
  SendMessage* const send_message_;
  ReceiveMessage* const receive_message_;
  grpc_event_engine::experimental::EventEngine* event_engine_;
};

class ClientCallData : public BaseCallData {
 public:
  ClientCallData(grpc_call_element* elem, const grpc_call_element_args* args,
                 uint8_t flags);
  ~ClientCallData() override;

  // Activity implementation.
  void ForceImmediateRepoll(WakeupMask) final;
  // Handle one grpc_transport_stream_op_batch
  void StartBatch(grpc_transport_stream_op_batch* batch) override;

  std::string DebugTag() const override;

 private:
  // At what stage is our handling of send initial metadata?
  enum class SendInitialState {
    // Start state: no op seen
    kInitial,
    // We've seen the op, and started the promise in response to it, but have
    // not yet sent the op to the next filter.
    kQueued,
    // We've sent the op to the next filter.
    kForwarded,
    // We were cancelled.
    kCancelled
  };
  // At what stage is our handling of recv trailing metadata?
  enum class RecvTrailingState {
    // Start state: no op seen
    kInitial,
    // We saw the op, and since it was bundled with send initial metadata, we
    // queued it until the send initial metadata can be sent to the next
    // filter.
    kQueued,
    // We've forwarded the op to the next filter.
    kForwarded,
    // The op has completed from below, but we haven't yet forwarded it up
    // (the promise gets to interject and mutate it).
    kComplete,
    // We've called the recv_metadata_ready callback from the original
    // recv_trailing_metadata op that was presented to us.
    kResponded,
    // We've been cancelled and handled that locally.
    // (i.e. whilst the recv_trailing_metadata op is queued in this filter).
    kCancelled
  };

  static const char* StateString(SendInitialState);
  static const char* StateString(RecvTrailingState);
  std::string DebugString() const;

  struct RecvInitialMetadata;
  class PollContext;

  // Handle cancellation.
  void Cancel(grpc_error_handle error, Flusher* flusher);
  // Begin running the promise - which will ultimately take some initial
  // metadata and return some trailing metadata.
  void StartPromise(Flusher* flusher);
  // Interject our callback into the op batch for recv trailing metadata
  // ready. Stash a pointer to the trailing metadata that will be filled in,
  // so we can manipulate it later.
  void HookRecvTrailingMetadata(CapturedBatch batch);
  // Construct a promise that will "call" the next filter.
  // Effectively:
  //   - put the modified initial metadata into the batch to be sent down.
  //   - return a wrapper around PollTrailingMetadata as the promise.
  ArenaPromise<ServerMetadataHandle> MakeNextPromise(CallArgs call_args);
  // Wrapper to make it look like we're calling the next filter as a promise.
  // First poll: send the send_initial_metadata op down the stack.
  // All polls: await receiving the trailing metadata, then return it to the
  // application.
  Poll<ServerMetadataHandle> PollTrailingMetadata();
  static void RecvTrailingMetadataReadyCallback(void* arg,
                                                grpc_error_handle error);
  void RecvTrailingMetadataReady(grpc_error_handle error);
  void RecvInitialMetadataReady(grpc_error_handle error);
  // Given an error, fill in ServerMetadataHandle to represent that error.
  void SetStatusFromError(grpc_metadata_batch* metadata,
                          grpc_error_handle error);
  // Wakeup and poll the promise if appropriate.
  void WakeInsideCombiner(Flusher* flusher) override;
  void OnWakeup() override;

  absl::string_view ClientOrServerString() const override { return "CLI"; }

  // Contained promise
  ArenaPromise<ServerMetadataHandle> promise_;
  // Queued batch containing at least a send_initial_metadata op.
  CapturedBatch send_initial_metadata_batch_;
  // Pointer to where trailing metadata will be stored.
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
  // Trailing metadata as returned by the promise, if we hadn't received
  // trailing metadata from below yet (so we can substitute it in).
  ServerMetadataHandle cancelling_metadata_;
  // State tracking recv initial metadata for filters that care about it.
  RecvInitialMetadata* recv_initial_metadata_ = nullptr;
  // Closure to call when we're done with the trailing metadata.
  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  // Our closure pointing to RecvTrailingMetadataReadyCallback.
  grpc_closure recv_trailing_metadata_ready_;
  // Error received during cancellation.
  grpc_error_handle cancelled_error_;
  // State of the send_initial_metadata op.
  SendInitialState send_initial_state_ = SendInitialState::kInitial;
  // State of the recv_trailing_metadata op.
  RecvTrailingState recv_trailing_state_ = RecvTrailingState::kInitial;
  // Polling related data. Non-null if we're actively polling
  PollContext* poll_ctx_ = nullptr;
  // Initial metadata outstanding token
  ClientInitialMetadataOutstandingToken initial_metadata_outstanding_token_;
};

class ServerCallData : public BaseCallData {
 public:
  ServerCallData(grpc_call_element* elem, const grpc_call_element_args* args,
                 uint8_t flags);
  ~ServerCallData() override;

  // Activity implementation.
  void ForceImmediateRepoll(WakeupMask) final;
  // Handle one grpc_transport_stream_op_batch
  void StartBatch(grpc_transport_stream_op_batch* batch) override;

  std::string DebugTag() const override;

 protected:
  absl::string_view ClientOrServerString() const override { return "SVR"; }

 private:
  // At what stage is our handling of recv initial metadata?
  enum class RecvInitialState {
    // Start state: no op seen
    kInitial,
    // Op seen, and forwarded to the next filter.
    // Now waiting for the callback.
    kForwarded,
    // The op has completed from below, but we haven't yet forwarded it up
    // (the promise gets to interject and mutate it).
    kComplete,
    // We've sent the response to the next filter up.
    kResponded,
  };
  // At what stage is our handling of send trailing metadata?
  enum class SendTrailingState {
    // Start state: no op seen
    kInitial,
    // We saw the op, but it was with a send message op (or one was in progress)
    // - so we'll wait for that to complete before processing the trailing
    // metadata.
    kQueuedBehindSendMessage,
    // We saw the op, and are waiting for the promise to complete
    // to forward it. First however we need to close sends.
    kQueuedButHaventClosedSends,
    // We saw the op, and are waiting for the promise to complete
    // to forward it.
    kQueued,
    // We've forwarded the op to the next filter.
    kForwarded,
    // We were cancelled.
    kCancelled
  };

  static const char* StateString(RecvInitialState state);
  static const char* StateString(SendTrailingState state);
  std::string DebugString() const;

  class PollContext;
  struct SendInitialMetadata;

  // Shut things down when the call completes.
  void Completed(grpc_error_handle error, bool tarpit_cancellation,
                 Flusher* flusher);
  // Construct a promise that will "call" the next filter.
  // Effectively:
  //   - put the modified initial metadata into the batch being sent up.
  //   - return a wrapper around PollTrailingMetadata as the promise.
  ArenaPromise<ServerMetadataHandle> MakeNextPromise(CallArgs call_args);
  // Wrapper to make it look like we're calling the next filter as a promise.
  // All polls: await sending the trailing metadata, then foward it down the
  // stack.
  Poll<ServerMetadataHandle> PollTrailingMetadata();
  static void RecvInitialMetadataReadyCallback(void* arg,
                                               grpc_error_handle error);
  void RecvInitialMetadataReady(grpc_error_handle error);
  static void RecvTrailingMetadataReadyCallback(void* arg,
                                                grpc_error_handle error);
  void RecvTrailingMetadataReady(grpc_error_handle error);
  // Wakeup and poll the promise if appropriate.
  void WakeInsideCombiner(Flusher* flusher) override;
  void OnWakeup() override;

  // Contained promise
  ArenaPromise<ServerMetadataHandle> promise_;
  // Pointer to where initial metadata will be stored.
  grpc_metadata_batch* recv_initial_metadata_ = nullptr;
  // Pointer to where trailing metadata will be stored.
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
  // State for sending initial metadata.
  SendInitialMetadata* send_initial_metadata_ = nullptr;
  // Closure to call when we're done with the initial metadata.
  grpc_closure* original_recv_initial_metadata_ready_ = nullptr;
  // Our closure pointing to RecvInitialMetadataReadyCallback.
  grpc_closure recv_initial_metadata_ready_;
  // Closure to call when we're done with the trailing metadata.
  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  // Our closure pointing to RecvTrailingMetadataReadyCallback.
  grpc_closure recv_trailing_metadata_ready_;
  // Error received during cancellation.
  grpc_error_handle cancelled_error_;
  // Trailing metadata batch
  CapturedBatch send_trailing_metadata_batch_;
  // State of the send_initial_metadata op.
  RecvInitialState recv_initial_state_ = RecvInitialState::kInitial;
  // State of the recv_trailing_metadata op.
  SendTrailingState send_trailing_state_ = SendTrailingState::kInitial;
  // Current poll context (or nullptr if not polling).
  PollContext* poll_ctx_ = nullptr;
  // Whether to forward the recv_initial_metadata op at the end of promise
  // wakeup.
  bool forward_recv_initial_metadata_callback_ = false;
};

// Specific call data per channel filter.
// Note that we further specialize for clients and servers since their
// implementations are very different.
template <FilterEndpoint endpoint>
class CallData;

// Client implementation of call data.
template <>
class CallData<FilterEndpoint::kClient> : public ClientCallData {
 public:
  using ClientCallData::ClientCallData;
};

// Server implementation of call data.
template <>
class CallData<FilterEndpoint::kServer> : public ServerCallData {
 public:
  using ServerCallData::ServerCallData;
};

struct BaseCallDataMethods {
  static void SetPollsetOrPollsetSet(grpc_call_element* elem,
                                     grpc_polling_entity* pollent) {
    static_cast<BaseCallData*>(elem->call_data)->set_pollent(pollent);
  }

  static void DestructCallData(grpc_call_element* elem,
                               const grpc_call_final_info* final_info) {
    auto* cd = static_cast<BaseCallData*>(elem->call_data);
    cd->Finalize(final_info);
    cd->~BaseCallData();
  }

  static void StartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
    static_cast<BaseCallData*>(elem->call_data)->StartBatch(batch);
  }
};

template <typename CallData, uint8_t kFlags>
struct CallDataFilterWithFlagsMethods {
  static absl::Status InitCallElem(grpc_call_element* elem,
                                   const grpc_call_element_args* args) {
    new (elem->call_data) CallData(elem, args, kFlags);
    return absl::OkStatus();
  }

  static void DestroyCallElem(grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* then_schedule_closure) {
    BaseCallDataMethods::DestructCallData(elem, final_info);
    if ((kFlags & kFilterIsLast) != 0) {
      ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure, absl::OkStatus());
    } else {
      GPR_ASSERT(then_schedule_closure == nullptr);
    }
  }
};

struct ChannelFilterMethods {
  static ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      grpc_channel_element* elem, CallArgs call_args,
      NextPromiseFactory next_promise_factory) {
    return static_cast<ChannelFilter*>(elem->channel_data)
        ->MakeCallPromise(std::move(call_args),
                          std::move(next_promise_factory));
  }

  static void StartTransportOp(grpc_channel_element* elem,
                               grpc_transport_op* op) {
    if (!static_cast<ChannelFilter*>(elem->channel_data)
             ->StartTransportOp(op)) {
      grpc_channel_next_op(elem, op);
    }
  }

  static void PostInitChannelElem(grpc_channel_stack*,
                                  grpc_channel_element* elem) {
    static_cast<ChannelFilter*>(elem->channel_data)->PostInit();
  }

  static void DestroyChannelElem(grpc_channel_element* elem) {
    static_cast<ChannelFilter*>(elem->channel_data)->~ChannelFilter();
  }

  static void GetChannelInfo(grpc_channel_element* elem,
                             const grpc_channel_info* info) {
    if (!static_cast<ChannelFilter*>(elem->channel_data)
             ->GetChannelInfo(info)) {
      grpc_channel_next_get_info(elem, info);
    }
  }
};

template <typename F, uint8_t kFlags>
struct ChannelFilterWithFlagsMethods {
  static absl::Status InitChannelElem(grpc_channel_element* elem,
                                      grpc_channel_element_args* args) {
    GPR_ASSERT(args->is_last == ((kFlags & kFilterIsLast) != 0));
    auto status = F::Create(args->channel_args,
                            ChannelFilter::Args(args->channel_stack, elem));
    if (!status.ok()) {
      static_assert(
          sizeof(promise_filter_detail::InvalidChannelFilter) <= sizeof(F),
          "InvalidChannelFilter must fit in F");
      new (elem->channel_data) promise_filter_detail::InvalidChannelFilter();
      return absl_status_to_grpc_error(status.status());
    }
    new (elem->channel_data) F(std::move(*status));
    return absl::OkStatus();
  }
};

}  // namespace promise_filter_detail

// F implements ChannelFilter and :
// class SomeChannelFilter : public ChannelFilter {
//  public:
//   static absl::StatusOr<SomeChannelFilter> Create(
//       ChannelArgs channel_args, ChannelFilter::Args filter_args);
// };
template <typename F, FilterEndpoint kEndpoint, uint8_t kFlags = 0>
absl::enable_if_t<std::is_base_of<ChannelFilter, F>::value, grpc_channel_filter>
MakePromiseBasedFilter(const char* name) {
  using CallData = promise_filter_detail::CallData<kEndpoint>;

  return grpc_channel_filter{
      // start_transport_stream_op_batch
      promise_filter_detail::BaseCallDataMethods::StartTransportStreamOpBatch,
      // make_call_promise
      promise_filter_detail::ChannelFilterMethods::MakeCallPromise,
      // start_transport_op
      promise_filter_detail::ChannelFilterMethods::StartTransportOp,
      // sizeof_call_data
      sizeof(CallData),
      // init_call_elem
      promise_filter_detail::CallDataFilterWithFlagsMethods<
          CallData, kFlags>::InitCallElem,
      // set_pollset_or_pollset_set
      promise_filter_detail::BaseCallDataMethods::SetPollsetOrPollsetSet,
      // destroy_call_elem
      promise_filter_detail::CallDataFilterWithFlagsMethods<
          CallData, kFlags>::DestroyCallElem,
      // sizeof_channel_data
      sizeof(F),
      // init_channel_elem
      promise_filter_detail::ChannelFilterWithFlagsMethods<
          F, kFlags>::InitChannelElem,
      // post_init_channel_elem
      promise_filter_detail::ChannelFilterMethods::PostInitChannelElem,
      // destroy_channel_elem
      promise_filter_detail::ChannelFilterMethods::DestroyChannelElem,
      // get_channel_info
      promise_filter_detail::ChannelFilterMethods::GetChannelInfo,
      // name
      name,
  };
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_CHANNEL_PROMISE_BASED_FILTER_H
