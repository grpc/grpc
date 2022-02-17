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

#ifndef GRPC_CORE_LIB_CHANNEL_PROMISE_BASED_FILTER_H
#define GRPC_CORE_LIB_CHANNEL_PROMISE_BASED_FILTER_H

// Scaffolding to allow the per-call part of a filter to be authored in a
// promise-style. Most of this will be removed once the promises conversion is
// completed.

#include <grpc/support/port_platform.h>

#include "absl/utility/utility.h"

#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

class ChannelFilter {
 public:
  class Args {
   public:
    Args() : Args(nullptr) {}
    explicit Args(grpc_channel_stack* channel_stack)
        : channel_stack_(channel_stack) {}

    grpc_channel_stack* channel_stack() const { return channel_stack_; }

   private:
    friend class ChannelFilter;
    grpc_channel_stack* channel_stack_;
  };

  // Construct a promise for one call.
  virtual ArenaPromise<TrailingMetadata> MakeCallPromise(
      ClientInitialMetadata initial_metadata,
      NextPromiseFactory next_promise_factory) = 0;

  // Start a legacy transport op
  // Return true if the op was handled, false if it should be passed to the
  // next filter.
  // TODO(ctiller): design a new API for this - we probably don't want big op
  // structures going forward.
  virtual bool StartTransportOp(grpc_transport_op*) { return false; }

 protected:
  virtual ~ChannelFilter() = default;
};

// Designator for whether a filter is client side or server side.
// Please don't use this outside calls to MakePromiseBasedFilter - it's intended
// to be deleted once the promise conversion is complete.
enum class FilterEndpoint {
  kClient,
  kServer,
};

namespace promise_filter_detail {

// Call data shared between all implementations of promise-based filters.
class BaseCallData {
 public:
  BaseCallData(grpc_call_element* elem, const grpc_call_element_args* args)
      : elem_(elem),
        arena_(args->arena),
        call_combiner_(args->call_combiner),
        deadline_(args->deadline) {}

 protected:
  class ScopedContext : public promise_detail::Context<Arena> {
   public:
    explicit ScopedContext(BaseCallData* call_data)
        : promise_detail::Context<Arena>(call_data->arena_) {}
  };

  static MetadataHandle<grpc_metadata_batch> WrapMetadata(
      grpc_metadata_batch* p) {
    return MetadataHandle<grpc_metadata_batch>(p);
  }

  static grpc_metadata_batch* UnwrapMetadata(
      MetadataHandle<grpc_metadata_batch> p) {
    return p.Unwrap();
  }

  grpc_call_element* elem() const { return elem_; }
  CallCombiner* call_combiner() const { return call_combiner_; }
  grpc_millis deadline() const { return deadline_; }

 private:
  grpc_call_element* const elem_;
  Arena* const arena_;
  CallCombiner* const call_combiner_;
  const grpc_millis deadline_;
};

// Specific call data per channel filter.
// Note that we further specialize for clients and servers since their
// implementations are very different.
template <class ChannelFilter, FilterEndpoint endpoint>
class CallData;

// Client implementation of call data.
template <class ChannelFilter>
class CallData<ChannelFilter, FilterEndpoint::kClient> : public BaseCallData {
 public:
  CallData(grpc_call_element* elem, const grpc_call_element_args* args)
      : BaseCallData(elem, args) {
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                      RecvTrailingMetadataReadyCallback, this,
                      grpc_schedule_on_exec_ctx);
  }

  ~CallData() {
    GPR_ASSERT(!is_polling_);
    GRPC_ERROR_UNREF(cancelled_error_);
  }

  // Handle one grpc_transport_stream_op_batch
  void StartBatch(grpc_transport_stream_op_batch* batch) {
    // Fake out the activity based context.
    ScopedContext context(this);

    // If this is a cancel stream, cancel anything we have pending and propagate
    // the cancellation.
    if (batch->cancel_stream) {
      GPR_ASSERT(!batch->send_initial_metadata &&
                 !batch->send_trailing_metadata && !batch->send_message &&
                 !batch->recv_initial_metadata && !batch->recv_message &&
                 !batch->recv_trailing_metadata);
      Cancel(batch->payload->cancel_stream.cancel_error);
      grpc_call_next_op(elem(), batch);
      return;
    }

    // send_initial_metadata: seeing this triggers the start of the promise part
    // of this filter.
    if (batch->send_initial_metadata) {
      // If we're already cancelled, just terminate the batch.
      if (send_initial_state_ == SendInitialState::kCancelled) {
        grpc_transport_stream_op_batch_finish_with_failure(
            batch, GRPC_ERROR_REF(cancelled_error_), call_combiner());
        return;
      }
      // Otherwise, we should not have seen a send_initial_metadata op yet.
      GPR_ASSERT(send_initial_state_ == SendInitialState::kInitial);
      // Mark ourselves as queued.
      send_initial_state_ = SendInitialState::kQueued;
      if (batch->recv_trailing_metadata) {
        // If there's a recv_trailing_metadata op, we queue that too.
        GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kInitial);
        recv_trailing_state_ = RecvTrailingState::kQueued;
      }
      // This is the queuing!
      send_initial_metadata_batch_ = batch;
      // And kick start the promise.
      StartPromise();
      return;
    }

    // recv_trailing_metadata *without* send_initial_metadata: hook it so we can
    // respond to it, and push it down.
    if (batch->recv_trailing_metadata) {
      GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kInitial);
      recv_trailing_state_ = RecvTrailingState::kForwarded;
      HookRecvTrailingMetadata(batch);
    }

    grpc_call_next_op(elem(), batch);
  }

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
    // queued it until the send initial metadata can be sent to the next filter.
    kQueued,
    // We've forwarded the op to the next filter.
    kForwarded,
    // The op has completed from below, but we haven't yet forwarded it up (the
    // promise gets to interject and mutate it).
    kComplete,
    // We've called the recv_metadata_ready callback from the original
    // recv_trailing_metadata op that was presented to us.
    kResponded,
    // We've been cancelled and handled that locally.
    // (i.e. whilst the recv_trailing_metadata op is queued in this filter).
    kCancelled
  };

  // Handle cancellation.
  void Cancel(grpc_error_handle error) {
    // Track the latest reason for cancellation.
    GRPC_ERROR_UNREF(cancelled_error_);
    cancelled_error_ = GRPC_ERROR_REF(error);
    // Stop running the promise.
    promise_ = ArenaPromise<TrailingMetadata>();
    // If we have an op queued, fail that op.
    // Record what we've done.
    if (send_initial_state_ == SendInitialState::kQueued) {
      send_initial_state_ = SendInitialState::kCancelled;
      if (recv_trailing_state_ == RecvTrailingState::kQueued) {
        recv_trailing_state_ = RecvTrailingState::kCancelled;
      }
      grpc_transport_stream_op_batch_finish_with_failure(
          absl::exchange(send_initial_metadata_batch_, nullptr),
          GRPC_ERROR_REF(cancelled_error_), call_combiner());
    } else {
      send_initial_state_ = SendInitialState::kCancelled;
    }
  }

  // Begin running the promise - which will ultimately take some initial
  // metadata and return some trailing metadata.
  void StartPromise() {
    GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);
    ChannelFilter* filter = static_cast<ChannelFilter*>(elem()->channel_data);

    // Construct the promise.
    promise_ = filter->MakeCallPromise(
        WrapMetadata(send_initial_metadata_batch_->payload
                         ->send_initial_metadata.send_initial_metadata),
        [this](ClientInitialMetadata initial_metadata) {
          return MakeNextPromise(std::move(initial_metadata));
        });
    // Poll once.
    WakeInsideCombiner();
  }

  // Interject our callback into the op batch for recv trailing metadata ready.
  // Stash a pointer to the trailing metadata that will be filled in, so we can
  // manipulate it later.
  void HookRecvTrailingMetadata(grpc_transport_stream_op_batch* batch) {
    recv_trailing_metadata_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata;
    original_recv_trailing_metadata_ready_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &recv_trailing_metadata_ready_;
  }

  // Construct a promise that will "call" the next filter.
  // Effectively:
  //   - put the modified initial metadata into the batch to be sent down.
  //   - return a wrapper around PollTrailingMetadata as the promise.
  ArenaPromise<TrailingMetadata> MakeNextPromise(
      ClientInitialMetadata initial_metadata) {
    GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);
    send_initial_metadata_batch_->payload->send_initial_metadata
        .send_initial_metadata = UnwrapMetadata(std::move(initial_metadata));
    return ArenaPromise<TrailingMetadata>(
        [this]() { return PollTrailingMetadata(); });
  }

  // Wrapper to make it look like we're calling the next filter as a promise.
  // First poll: send the send_initial_metadata op down the stack.
  // All polls: await receiving the trailing metadata, then return it to the
  // application.
  Poll<TrailingMetadata> PollTrailingMetadata() {
    if (send_initial_state_ == SendInitialState::kQueued) {
      // First poll: pass the send_initial_metadata op down the stack.
      GPR_ASSERT(send_initial_metadata_batch_ != nullptr);
      send_initial_state_ = SendInitialState::kForwarded;
      if (recv_trailing_state_ == RecvTrailingState::kQueued) {
        // (and the recv_trailing_metadata op if it's part of the queuing)
        HookRecvTrailingMetadata(send_initial_metadata_batch_);
        recv_trailing_state_ = RecvTrailingState::kForwarded;
      }
      forward_send_initial_metadata_ = true;
    }
    switch (recv_trailing_state_) {
      case RecvTrailingState::kInitial:
      case RecvTrailingState::kQueued:
      case RecvTrailingState::kForwarded:
        // No trailing metadata yet: we are pending.
        // We return that and expect the promise to be repolled later (if it's
        // not cancelled).
        return Pending{};
      case RecvTrailingState::kComplete:
        // We've received trailing metadata: pass it to the promise and allow it
        // to adjust it.
        return WrapMetadata(recv_trailing_metadata_);
      case RecvTrailingState::kCancelled: {
        // We've been cancelled: synthesize some trailing metadata and pass it
        // to the calling promise for adjustment.
        recv_trailing_metadata_->Clear();
        SetStatusFromError(recv_trailing_metadata_, cancelled_error_);
        return WrapMetadata(recv_trailing_metadata_);
      }
      case RecvTrailingState::kResponded:
        // We've already responded to the caller: we can't do anything and we
        // should never reach here.
        abort();
    }
    GPR_UNREACHABLE_CODE(return Pending{});
  }

  static void RecvTrailingMetadataReadyCallback(void* arg,
                                                grpc_error_handle error) {
    static_cast<CallData*>(arg)->RecvTrailingMetadataReady(error);
  }

  void RecvTrailingMetadataReady(grpc_error_handle error) {
    // If there was an error, we'll put that into the trailing metadata and
    // proceed as if there was not.
    if (error != GRPC_ERROR_NONE) {
      SetStatusFromError(recv_trailing_metadata_, error);
    }
    // Record that we've got the callback.
    GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kForwarded);
    recv_trailing_state_ = RecvTrailingState::kComplete;
    // Repoll the promise.
    ScopedContext context(this);
    WakeInsideCombiner();
  }

  // Given an error, fill in TrailingMetadata to represent that error.
  void SetStatusFromError(grpc_metadata_batch* metadata,
                          grpc_error_handle error) {
    grpc_status_code status_code = GRPC_STATUS_UNKNOWN;
    std::string status_details;
    grpc_error_get_status(error, deadline(), &status_code, &status_details,
                          nullptr, nullptr);
    metadata->Set(GrpcStatusMetadata(), status_code);
    metadata->Set(GrpcMessageMetadata(),
                  Slice::FromCopiedString(status_details));
    metadata->GetOrCreatePointer(GrpcStatusContext())
        ->emplace_back(grpc_error_std_string(error));
  }

  // Wakeup and poll the promise if appropriate.
  void WakeInsideCombiner() {
    GPR_ASSERT(!is_polling_);
    grpc_closure* call_closure = nullptr;
    is_polling_ = true;
    switch (send_initial_state_) {
      case SendInitialState::kQueued:
      case SendInitialState::kForwarded: {
        // Poll the promise once since we're waiting for it.
        Poll<TrailingMetadata> poll = promise_();
        if (auto* r = absl::get_if<TrailingMetadata>(&poll)) {
          GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kComplete);
          GPR_ASSERT(recv_trailing_metadata_ == UnwrapMetadata(std::move(*r)));
          recv_trailing_state_ = RecvTrailingState::kResponded;
          call_closure =
              absl::exchange(original_recv_trailing_metadata_ready_, nullptr);
        }
      } break;
      case SendInitialState::kInitial:
      case SendInitialState::kCancelled:
        // If we get a response without sending anything, we just propagate that
        // up. (note: that situation isn't possible once we finish the promise
        // transition).
        if (recv_trailing_state_ == RecvTrailingState::kComplete) {
          recv_trailing_state_ = RecvTrailingState::kResponded;
          call_closure =
              absl::exchange(original_recv_trailing_metadata_ready_, nullptr);
        }
        break;
    }
    is_polling_ = false;
    if (absl::exchange(forward_send_initial_metadata_, false)) {
      grpc_call_next_op(elem(),
                        absl::exchange(send_initial_metadata_batch_, nullptr));
    }
    if (call_closure != nullptr) {
      Closure::Run(DEBUG_LOCATION, call_closure, GRPC_ERROR_NONE);
    }
  }

  // Contained promise
  ArenaPromise<TrailingMetadata> promise_;
  // Queued batch containing at least a send_initial_metadata op.
  grpc_transport_stream_op_batch* send_initial_metadata_batch_ = nullptr;
  // Pointer to where trailing metadata will be stored.
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
  // Closure to call when we're done with the trailing metadata.
  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  // Our closure pointing to RecvTrailingMetadataReadyCallback.
  grpc_closure recv_trailing_metadata_ready_;
  // Error received during cancellation.
  grpc_error_handle cancelled_error_ = GRPC_ERROR_NONE;
  // State of the send_initial_metadata op.
  SendInitialState send_initial_state_ = SendInitialState::kInitial;
  // State of the recv_trailing_metadata op.
  RecvTrailingState recv_trailing_state_ = RecvTrailingState::kInitial;
  // Whether we're currently polling the promise.
  bool is_polling_ = false;
  // Whether we should forward send initial metadata after polling?
  bool forward_send_initial_metadata_ = false;
};

// Server implementation of call data.
template <class ChannelFilter>
class CallData<ChannelFilter, FilterEndpoint::kServer> : public BaseCallData {
 public:
  CallData(grpc_call_element* elem, const grpc_call_element_args* args)
      : BaseCallData(elem, args) {
    GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_,
                      RecvInitialMetadataReadyCallback, this,
                      grpc_schedule_on_exec_ctx);
  }

  ~CallData() {
    GPR_ASSERT(!is_polling_);
    GRPC_ERROR_UNREF(cancelled_error_);
  }

  // Handle one grpc_transport_stream_op_batch
  void StartBatch(grpc_transport_stream_op_batch* batch) {
    // Fake out the activity based context.
    ScopedContext context(this);

    // If this is a cancel stream, cancel anything we have pending and propagate
    // the cancellation.
    if (batch->cancel_stream) {
      GPR_ASSERT(!batch->send_initial_metadata &&
                 !batch->send_trailing_metadata && !batch->send_message &&
                 !batch->recv_initial_metadata && !batch->recv_message &&
                 !batch->recv_trailing_metadata);
      Cancel(batch->payload->cancel_stream.cancel_error);
      grpc_call_next_op(elem(), batch);
      return;
    }

    // recv_initial_metadata: we hook the response of this so we can start the
    // promise at an appropriate time.
    if (batch->recv_initial_metadata) {
      GPR_ASSERT(!batch->send_initial_metadata &&
                 !batch->send_trailing_metadata && !batch->send_message &&
                 !batch->recv_message && !batch->recv_trailing_metadata);
      // Otherwise, we should not have seen a send_initial_metadata op yet.
      GPR_ASSERT(recv_initial_state_ == RecvInitialState::kInitial);
      // Hook the callback so we know when to start the promise.
      recv_initial_metadata_ =
          batch->payload->recv_initial_metadata.recv_initial_metadata;
      original_recv_initial_metadata_ready_ =
          batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
      batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
          &recv_initial_metadata_ready_;
      recv_initial_state_ = RecvInitialState::kForwarded;
    }

    // send_trailing_metadata
    if (batch->send_trailing_metadata) {
      switch (send_trailing_state_) {
        case SendTrailingState::kInitial:
          send_trailing_metadata_batch_ = batch;
          send_trailing_state_ = SendTrailingState::kQueued;
          WakeInsideCombiner([this](grpc_error_handle error) {
            GPR_ASSERT(send_trailing_state_ == SendTrailingState::kQueued);
            Cancel(error);
          });
          break;
        case SendTrailingState::kQueued:
        case SendTrailingState::kForwarded:
          abort();  // unreachable
          break;
        case SendTrailingState::kCancelled:
          abort();  // unimplemented
          break;
      }
      return;
    }

    grpc_call_next_op(elem(), batch);
  }

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
    // We saw the op, and are waiting for the promise to complete
    // to forward it.
    kQueued,
    // We've forwarded the op to the next filter.
    kForwarded,
    // We were cancelled.
    kCancelled
  };

  // Handle cancellation.
  void Cancel(grpc_error_handle error) {
    // Track the latest reason for cancellation.
    GRPC_ERROR_UNREF(cancelled_error_);
    cancelled_error_ = GRPC_ERROR_REF(error);
    // Stop running the promise.
    promise_ = ArenaPromise<TrailingMetadata>();
    if (send_trailing_state_ == SendTrailingState::kQueued) {
      send_trailing_state_ = SendTrailingState::kCancelled;
      grpc_transport_stream_op_batch_finish_with_failure(
          absl::exchange(send_trailing_metadata_batch_, nullptr),
          GRPC_ERROR_REF(cancelled_error_), call_combiner());
    } else {
      send_trailing_state_ = SendTrailingState::kCancelled;
    }
  }

  // Construct a promise that will "call" the next filter.
  // Effectively:
  //   - put the modified initial metadata into the batch being sent up.
  //   - return a wrapper around PollTrailingMetadata as the promise.
  ArenaPromise<TrailingMetadata> MakeNextPromise(
      ClientInitialMetadata initial_metadata) {
    GPR_ASSERT(recv_initial_state_ == RecvInitialState::kComplete);
    GPR_ASSERT(UnwrapMetadata(std::move(initial_metadata)) ==
               recv_initial_metadata_);
    forward_recv_initial_metadata_callback_ = true;
    return ArenaPromise<TrailingMetadata>(
        [this]() { return PollTrailingMetadata(); });
  }

  // Wrapper to make it look like we're calling the next filter as a promise.
  // All polls: await sending the trailing metadata, then foward it down the
  // stack.
  Poll<TrailingMetadata> PollTrailingMetadata() {
    switch (send_trailing_state_) {
      case SendTrailingState::kInitial:
        return Pending{};
      case SendTrailingState::kQueued:
        return WrapMetadata(
            send_trailing_metadata_batch_->payload->send_trailing_metadata
                .send_trailing_metadata);
      case SendTrailingState::kForwarded:
        abort();  // unreachable
      case SendTrailingState::kCancelled:
        // We could translate cancelled_error to metadata and return it... BUT
        // we're not gonna be running much longer and the results going to be
        // ignored.
        return Pending{};
    }
    GPR_UNREACHABLE_CODE(return Pending{});
  }

  static void RecvInitialMetadataReadyCallback(void* arg,
                                               grpc_error_handle error) {
    static_cast<CallData*>(arg)->RecvInitialMetadataReady(error);
  }

  void RecvInitialMetadataReady(grpc_error_handle error) {
    GPR_ASSERT(recv_initial_state_ == RecvInitialState::kForwarded);
    // If there was an error we just propagate that through
    if (error != GRPC_ERROR_NONE) {
      recv_initial_state_ = RecvInitialState::kResponded;
      Closure::Run(DEBUG_LOCATION, original_recv_initial_metadata_ready_,
                   GRPC_ERROR_REF(error));
      return;
    }
    // Record that we've got the callback.
    recv_initial_state_ = RecvInitialState::kComplete;

    // Start the promise.
    ScopedContext context(this);
    // Construct the promise.
    ChannelFilter* filter = static_cast<ChannelFilter*>(elem()->channel_data);
    promise_ = filter->MakeCallPromise(
        WrapMetadata(recv_initial_metadata_),
        [this](ClientInitialMetadata initial_metadata) {
          return MakeNextPromise(std::move(initial_metadata));
        });
    // Poll once.
    bool own_error = false;
    WakeInsideCombiner([&error, &own_error](grpc_error_handle new_error) {
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      error = GRPC_ERROR_REF(new_error);
      own_error = true;
    });
    Closure::Run(DEBUG_LOCATION, original_recv_initial_metadata_ready_,
                 GRPC_ERROR_REF(error));
    if (own_error) GRPC_ERROR_UNREF(error);
  }

  // Wakeup and poll the promise if appropriate.
  void WakeInsideCombiner(absl::FunctionRef<void(grpc_error_handle)> cancel) {
    GPR_ASSERT(!is_polling_);
    bool forward_send_trailing_metadata = false;
    is_polling_ = true;
    if (recv_initial_state_ == RecvInitialState::kComplete) {
      Poll<TrailingMetadata> poll = promise_();
      if (auto* r = absl::get_if<TrailingMetadata>(&poll)) {
        auto* md = UnwrapMetadata(std::move(*r));
        bool destroy_md = true;
        switch (send_trailing_state_) {
          case SendTrailingState::kQueued: {
            if (send_trailing_metadata_batch_->payload->send_trailing_metadata
                    .send_trailing_metadata != md) {
              *send_trailing_metadata_batch_->payload->send_trailing_metadata
                   .send_trailing_metadata = std::move(*md);
            } else {
              destroy_md = false;
            }
            forward_send_trailing_metadata = true;
          } break;
          case SendTrailingState::kForwarded:
            abort();  // unreachable
            break;
          case SendTrailingState::kInitial: {
            GPR_ASSERT(*md->get_pointer(GrpcStatusMetadata()) !=
                       GRPC_STATUS_OK);
            grpc_error_handle error = grpc_error_set_int(
                GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "early return from promise based filter"),
                GRPC_ERROR_INT_GRPC_STATUS,
                *md->get_pointer(GrpcStatusMetadata()));
            if (auto* message = md->get_pointer(GrpcMessageMetadata())) {
              error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE,
                                         message->as_string_view());
            }
            cancel(error);
            GRPC_ERROR_UNREF(error);
          } break;
          case SendTrailingState::kCancelled:
            // Nothing to do.
            break;
        }
        if (destroy_md) {
          md->~grpc_metadata_batch();
        }
      }
    }
    is_polling_ = false;
    if (forward_send_trailing_metadata) {
      grpc_call_next_op(elem(),
                        absl::exchange(send_trailing_metadata_batch_, nullptr));
    }
  }

  // Contained promise
  ArenaPromise<TrailingMetadata> promise_;
  // Pointer to where initial metadata will be stored.
  grpc_metadata_batch* recv_initial_metadata_ = nullptr;
  // Closure to call when we're done with the trailing metadata.
  grpc_closure* original_recv_initial_metadata_ready_ = nullptr;
  // Our closure pointing to RecvInitialMetadataReadyCallback.
  grpc_closure recv_initial_metadata_ready_;
  // Error received during cancellation.
  grpc_error_handle cancelled_error_ = GRPC_ERROR_NONE;
  // Trailing metadata batch
  grpc_transport_stream_op_batch* send_trailing_metadata_batch_ = nullptr;
  // State of the send_initial_metadata op.
  RecvInitialState recv_initial_state_ = RecvInitialState::kInitial;
  // State of the recv_trailing_metadata op.
  SendTrailingState send_trailing_state_ = SendTrailingState::kInitial;
  // Whether we're currently polling the promise.
  bool is_polling_ = false;
  // Whether to forward the recv_initial_metadata op at the end of promise
  // wakeup.
  bool forward_recv_initial_metadata_callback_ = false;
};

}  // namespace promise_filter_detail

// F implements ChannelFilter and :
// class SomeChannelFilter : public ChannelFilter {
//  public:
//   static absl::StatusOr<SomeChannelFilter> Create(
//       ChannelFilter::Args filter_args);
// };
// TODO(ctiller): allow implementing get_channel_info, start_transport_op in
// some way on ChannelFilter.
template <typename F, FilterEndpoint kEndpoint>
absl::enable_if_t<std::is_base_of<ChannelFilter, F>::value, grpc_channel_filter>
MakePromiseBasedFilter(const char* name) {
  using CallData = promise_filter_detail::CallData<F, kEndpoint>;

  return grpc_channel_filter{
      // start_transport_stream_op_batch
      [](grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
        static_cast<CallData*>(elem->call_data)->StartBatch(batch);
      },
      // make_call_promise
      [](grpc_channel_element* elem, ClientInitialMetadata initial_metadata,
         NextPromiseFactory next_promise_factory) {
        return static_cast<F*>(elem->channel_data)
            ->MakeCallPromise(std::move(initial_metadata),
                              std::move(next_promise_factory));
      },
      // start_transport_op
      [](grpc_channel_element* elem, grpc_transport_op* op) {
        if (!static_cast<F*>(elem->channel_data)->StartTransportOp(op)) {
          grpc_channel_next_op(elem, op);
        }
      },
      // sizeof_call_data
      sizeof(CallData),
      // init_call_elem
      [](grpc_call_element* elem, const grpc_call_element_args* args) {
        new (elem->call_data) CallData(elem, args);
        return GRPC_ERROR_NONE;
      },
      // set_pollset_or_pollset_set
      grpc_call_stack_ignore_set_pollset_or_pollset_set,
      // destroy_call_elem
      [](grpc_call_element* elem, const grpc_call_final_info*, grpc_closure*) {
        static_cast<CallData*>(elem->call_data)->~CallData();
      },
      // sizeof_channel_data
      sizeof(F),
      // init_channel_elem
      [](grpc_channel_element* elem, grpc_channel_element_args* args) {
        GPR_ASSERT(!args->is_last);
        auto status = F::Create(args->channel_args,
                                ChannelFilter::Args(args->channel_stack));
        if (!status.ok()) return absl_status_to_grpc_error(status.status());
        new (elem->channel_data) F(std::move(*status));
        return GRPC_ERROR_NONE;
      },
      // destroy_channel_elem
      [](grpc_channel_element* elem) {
        static_cast<F*>(elem->channel_data)->~F();
      },
      // get_channel_info
      grpc_channel_next_get_info,
      // name
      name,
  };
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_CHANNEL_PROMISE_BASED_FILTER_H
