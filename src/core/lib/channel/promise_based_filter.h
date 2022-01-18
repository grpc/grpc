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
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

namespace promise_filter_detail {
class BaseCallData;
};

// Small unowned "handle" type to ensure one accessor at a time to metadata.
// The focus here is to get promises to use the syntax we'd like - we'll
// probably substitute some other smart pointer later.
template <typename T>
class MetadataHandle {
 public:
  MetadataHandle() = default;

  MetadataHandle(const MetadataHandle&) = delete;
  MetadataHandle& operator=(const MetadataHandle&) = delete;

  MetadataHandle(MetadataHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }
  MetadataHandle& operator=(MetadataHandle&& other) noexcept {
    handle_ = other.handle_;
    other.handle_ = nullptr;
    return *this;
  }

  T* operator->() const { return handle_; }
  bool has_value() const { return handle_ != nullptr; }

 private:
  friend class promise_filter_detail::BaseCallData;

  explicit MetadataHandle(T* handle) : handle_(handle) {}
  T* Unwrap() {
    T* result = handle_;
    handle_ = nullptr;
    return result;
  }

  T* handle_ = nullptr;
};

// Trailing metadata type
// TODO(ctiller): This should be a bespoke instance of MetadataMap<>
using TrailingMetadata = MetadataHandle<grpc_metadata_batch>;

// Initial metadata type
// TODO(ctiller): This should be a bespoke instance of MetadataMap<>
using InitialMetadata = MetadataHandle<grpc_metadata_batch>;

using NextPromiseFactory =
    std::function<ArenaPromise<TrailingMetadata>(InitialMetadata)>;

namespace promise_filter_detail {

// Call data shared between all implementations of promise-based filters.
class BaseCallData {
 public:
  explicit BaseCallData(const grpc_call_element_args* args)
      : arena_(args->arena),
        call_combiner_(args->call_combiner),
        owning_call_(args->call_stack),
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

  Arena* const arena_;
  CallCombiner* const call_combiner_;
  grpc_call_stack* const owning_call_;
  const grpc_millis deadline_;
  ArenaPromise<TrailingMetadata> promise_;
};

// Specific call data per channel filter.
// Note that we further specialize for clients and servers since their
// implementations are very different.
template <class ChannelFilter, bool kIsClient = ChannelFilter::is_client()>
class CallData;

// Client implementation of call data.
template <class ChannelFilter>
class CallData<ChannelFilter, true> : public BaseCallData {
 public:
  explicit CallData(const grpc_call_element_args* args) : BaseCallData(args) {
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                      RecvTrailingMetadataReadyCallback, this,
                      grpc_schedule_on_exec_ctx);
  }

  ~CallData() {
    GPR_ASSERT(!is_polling_);
    GRPC_ERROR_UNREF(cancelled_error_);
  }

  // Handle one grpc_transport_stream_op_batch
  void Op(grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
    // Fake out the activity based context.
    ScopedContext context(this);

    // If this is a cancel stream, cancel anything we have pending and propagate
    // the cancellation.
    if (op->cancel_stream) {
      GPR_ASSERT(!op->send_initial_metadata && !op->send_trailing_metadata &&
                 !op->send_message && !op->recv_initial_metadata &&
                 !op->recv_message && !op->recv_trailing_metadata);
      Cancel(op->payload->cancel_stream.cancel_error);
      grpc_call_next_op(elem, op);
      return;
    }

    // send_initial_metadata: seeing this triggers the start of the promise part
    // of this filter.
    if (op->send_initial_metadata) {
      // If we're already cancelled, just terminate the batch.
      if (send_initial_state_ == SendInitialState::kCancelled) {
        grpc_transport_stream_op_batch_finish_with_failure(
            op, GRPC_ERROR_REF(cancelled_error_), call_combiner_);
        return;
      }
      // Otherwise, we should not have seen a send_initial_metadata op yet.
      GPR_ASSERT(send_initial_state_ == SendInitialState::kInitial);
      // Mark ourselves as queued.
      send_initial_state_ = SendInitialState::kQueued;
      if (op->recv_trailing_metadata) {
        // If there's a recv_trailing_metadata op, we queue that too.
        GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kInitial);
        recv_trailing_state_ = RecvTrailingState::kQueued;
      }
      // This is the queuing!
      send_initial_metadata_batch_ = op;
      // And kick start the promise.
      StartPromise(elem);
      return;
    }

    // recv_trailing_metadata *without* send_initial_metadata: hook it so we can
    // respond to it, and push it down.
    if (op->recv_trailing_metadata) {
      GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kInitial);
      recv_trailing_state_ = RecvTrailingState::kForwarded;
      HookRecvTrailingMetadata(op);
    }

    grpc_call_next_op(elem, op);
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
          GRPC_ERROR_REF(cancelled_error_), call_combiner_);
    } else {
      send_initial_state_ = SendInitialState::kCancelled;
    }
  }

  // Begin running the promise - which will ultimately take some initial
  // metadata and return some trailing metadata.
  void StartPromise(grpc_call_element* elem) {
    GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);
    ChannelFilter* filter = static_cast<ChannelFilter*>(elem->channel_data);

    // Construct the promise.
    promise_ = filter->MakeCallPromise(
        WrapMetadata(send_initial_metadata_batch_->payload
                         ->send_initial_metadata.send_initial_metadata),
        [elem](InitialMetadata initial_metadata) {
          return static_cast<CallData*>(elem->call_data)
              ->NextPromiseFactory(elem, std::move(initial_metadata));
        });
    // Poll once.
    WakeInsideCombiner();
  }

  // Interject our callback into the op batch for recv trailing metadata ready.
  // Stash a pointer to the trailing metadata that will be filled in, so we can
  // manipulate it later.
  void HookRecvTrailingMetadata(grpc_transport_stream_op_batch* op) {
    recv_trailing_metadata_ =
        op->payload->recv_trailing_metadata.recv_trailing_metadata;
    original_recv_trailing_metadata_ready_ =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    op->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &recv_trailing_metadata_ready_;
  }

  // Construct a promise that will "call" the next filter.
  // Effectively:
  //   - put the modified initial metadata into the batch to be sent down.
  //   - return a wrapper around PollTrailingMetadata as the promise.
  ArenaPromise<TrailingMetadata> NextPromiseFactory(
      grpc_call_element* elem, InitialMetadata initial_metadata) {
    GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);
    send_initial_metadata_batch_->payload->send_initial_metadata
        .send_initial_metadata = UnwrapMetadata(std::move(initial_metadata));
    return ArenaPromise<TrailingMetadata>([elem]() {
      return static_cast<CallData*>(elem->call_data)
          ->PollTrailingMetadata(elem);
    });
  }

  // Wrapper to make it look like we're calling the next filter as a promise.
  // First poll: send the send_initial_metadata op down the stack.
  // All polls: await receiving the trailing metadata, then return it to the
  // application.
  Poll<TrailingMetadata> PollTrailingMetadata(grpc_call_element* elem) {
    if (send_initial_state_ == SendInitialState::kQueued) {
      // First poll: pass the send_initial_metadata op down the stack.
      auto* op = absl::exchange(send_initial_metadata_batch_, nullptr);
      GPR_ASSERT(op != nullptr);
      send_initial_state_ = SendInitialState::kForwarded;
      if (recv_trailing_state_ == RecvTrailingState::kQueued) {
        // (and the recv_trailing_metadata op if it's part of the queuing)
        HookRecvTrailingMetadata(op);
        recv_trailing_state_ = RecvTrailingState::kForwarded;
      }
      grpc_call_next_op(elem, op);
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
    grpc_error_get_status(error, deadline_, &status_code, &status_details,
                          nullptr, nullptr);
    metadata->Set(GrpcStatusMetadata(), status_code);
    metadata->Set(GrpcMessageMetadata(),
                  Slice::FromCopiedString(status_details));
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
    if (call_closure != nullptr) {
      Closure::Run(DEBUG_LOCATION, call_closure, GRPC_ERROR_NONE);
    }
  }

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
};

}  // namespace promise_filter_detail

// ChannelFilter contains the following:
// class SomeChannelFilter {
//  public:
//   static constexpr bool is_client();
//   static constexpr const char* name();
//   static absl::StatusOr<SomeChannelFilter> Create(
//       const grpc_channel_args* args);
//   ArenaPromise<TrailingMetadata> MakeCallPromise(
//       InitialMetadata* initial_metadata, NextPromiseFactory next_promise);
// };
// TODO(ctiller): allow implementing get_channel_info, start_transport_op in
// some way on ChannelFilter.
template <typename ChannelFilter>
grpc_channel_filter MakePromiseBasedFilter() {
  using CallData = promise_filter_detail::CallData<ChannelFilter>;

  return grpc_channel_filter{
      // start_transport_stream_op_batch
      [](grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
        static_cast<CallData*>(elem->call_data)->Op(elem, op);
      },
      // start_transport_op - for now unsupported
      grpc_channel_next_op,
      // sizeof_call_data
      sizeof(CallData),
      // init_call_elem
      [](grpc_call_element* elem, const grpc_call_element_args* args) {
        new (elem->call_data) CallData(args);
        return GRPC_ERROR_NONE;
      },
      // set_pollset_or_pollset_set
      grpc_call_stack_ignore_set_pollset_or_pollset_set,
      // destroy_call_elem
      [](grpc_call_element* elem, const grpc_call_final_info*, grpc_closure*) {
        static_cast<CallData*>(elem->call_data)->~CallData();
      },
      // sizeof_channel_data
      sizeof(ChannelFilter),
      // init_channel_elem
      [](grpc_channel_element* elem, grpc_channel_element_args* args) {
        GPR_ASSERT(!args->is_last);
        auto status = ChannelFilter::Create(args->channel_args);
        if (!status.ok()) return absl_status_to_grpc_error(status.status());
        new (elem->channel_data) ChannelFilter(std::move(*status));
        return GRPC_ERROR_NONE;
      },
      // destroy_channel_elem
      [](grpc_channel_element* elem) {
        static_cast<ChannelFilter*>(elem->channel_data)->~ChannelFilter();
      },
      // get_channel_info
      grpc_channel_next_get_info,
      // name
      ChannelFilter::name(),
  };
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_CHANNEL_PROMISE_BASED_FILTER_H
