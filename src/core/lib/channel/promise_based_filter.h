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

// Trailing metadata type
// TODO(ctiller): This should be a bespoke instance of MetadataMap<>
using TrailingMetadata = grpc_metadata_batch;

// Initial metadata type
// TODO(ctiller): This should be a bespoke instance of MetadataMap<>
using InitialMetadata = grpc_metadata_batch;

using NextPromiseFactory =
    std::function<ArenaPromise<TrailingMetadata>(InitialMetadata*)>;

namespace promise_filter_detail {

class BaseCallData {
 public:
  explicit BaseCallData(const grpc_call_element_args* args)
      : arena_(args->arena),
        call_combiner_(args->call_combiner),
        deadline_(args->deadline) {}

 protected:
  class ScopedContext : public promise_detail::Context<Arena> {
   public:
    explicit ScopedContext(BaseCallData* call_data)
        : promise_detail::Context<Arena>(call_data->arena_) {}
  };

  Arena* const arena_;
  CallCombiner* const call_combiner_;
  const grpc_millis deadline_;
  ArenaPromise<TrailingMetadata> promise_;
};

template <class ChannelFilter, bool kIsClient = ChannelFilter::is_client()>
class CallData;

template <class ChannelFilter>
class CallData<ChannelFilter, true> : public BaseCallData {
 public:
  explicit CallData(const grpc_call_element_args* args) : BaseCallData(args) {
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                      RecvTrailingMetadataReadyCallback, this,
                      grpc_schedule_on_exec_ctx);
  }

  ~CallData() { GRPC_ERROR_UNREF(cancelled_error_); }

  void Op(grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
    ScopedContext context(this);

    if (op->cancel_stream) {
      GPR_ASSERT(!op->send_initial_metadata && !op->send_trailing_metadata &&
                 !op->send_message && !op->recv_initial_metadata &&
                 !op->recv_message && !op->recv_trailing_metadata);
      Cancel(elem, op->payload->cancel_stream.cancel_error);
      grpc_call_next_op(elem, op);
      return;
    }

    if (op->send_initial_metadata) {
      if (send_initial_state_ == SendInitialState::kCancelled) {
        grpc_transport_stream_op_batch_finish_with_failure(
            op, GRPC_ERROR_REF(cancelled_error_), call_combiner_);
        return;
      }
      GPR_ASSERT(send_initial_state_ == SendInitialState::kInitial);
      send_initial_state_ = SendInitialState::kQueued;
      if (op->recv_trailing_metadata) {
        GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kInitial);
        recv_trailing_state_ = RecvTrailingState::kQueued;
      }
      send_initial_metadata_batch_ = op;
      StartPromise(elem);
      return;
    }

    if (op->recv_trailing_metadata) {
      GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kInitial);
      recv_trailing_state_ = RecvTrailingState::kForwarded;
      HookRecvTrailingMetadata(op);
    }

    grpc_call_next_op(elem, op);
  }

 private:
  enum class SendInitialState { kInitial, kQueued, kForwarded, kCancelled };
  enum class RecvTrailingState {
    kInitial,
    kQueued,
    kForwarded,
    kComplete,
    kResponded,
    kCancelled
  };

  void Cancel(grpc_call_element* elem, grpc_error_handle error) {
    GRPC_ERROR_UNREF(cancelled_error_);
    cancelled_error_ = GRPC_ERROR_REF(error);
    promise_ = ArenaPromise<TrailingMetadata>();
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

  void StartPromise(grpc_call_element* elem) {
    GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);

    ChannelFilter* filter = static_cast<ChannelFilter*>(elem->channel_data);

    promise_ = filter->MakeCallPromise(
        send_initial_metadata_batch_->payload->send_initial_metadata
            .send_initial_metadata,
        [elem](InitialMetadata* initial_metadata) {
          return static_cast<CallData*>(elem->call_data)
              ->NextPromiseFactory(elem, initial_metadata);
        });
    WakeInsideCombiner();
  }

  void HookRecvTrailingMetadata(grpc_transport_stream_op_batch* op) {
    recv_trailing_metadata_ =
        op->payload->recv_trailing_metadata.recv_trailing_metadata;
    original_recv_trailing_metadata_ready_ =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    op->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &recv_trailing_metadata_ready_;
  }

  ArenaPromise<TrailingMetadata> NextPromiseFactory(
      grpc_call_element* elem, InitialMetadata* initial_metadata) {
    GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);
    send_initial_metadata_batch_->payload->send_initial_metadata
        .send_initial_metadata = initial_metadata;
    return ArenaPromise<TrailingMetadata>([elem]() {
      return static_cast<CallData*>(elem->call_data)
          ->PollTrailingMetadata(elem);
    });
  }

  Poll<TrailingMetadata> PollTrailingMetadata(grpc_call_element* elem) {
    if (send_initial_state_ == SendInitialState::kQueued) {
      auto* op = absl::exchange(send_initial_metadata_batch_, nullptr);
      GPR_ASSERT(op != nullptr);
      send_initial_state_ = SendInitialState::kForwarded;
      if (recv_trailing_state_ == RecvTrailingState::kQueued) {
        HookRecvTrailingMetadata(op);
        recv_trailing_state_ = RecvTrailingState::kForwarded;
      }
      grpc_call_next_op(elem, op);
    }
    switch (recv_trailing_state_) {
      case RecvTrailingState::kInitial:
      case RecvTrailingState::kQueued:
      case RecvTrailingState::kForwarded:
        return Pending{};
      case RecvTrailingState::kComplete:
        return std::move(*recv_trailing_metadata_);
      case RecvTrailingState::kCancelled: {
        TrailingMetadata synthetic(arena_);
        SetStatusFromError(&synthetic, cancelled_error_);
        return synthetic;
      }
      case RecvTrailingState::kResponded:
        abort();
    }
    GPR_UNREACHABLE_CODE(return Pending{});
  }

  static void RecvTrailingMetadataReadyCallback(void* arg,
                                                grpc_error_handle error) {
    static_cast<CallData*>(arg)->RecvTrailingMetadataReady(error);
  }

  void RecvTrailingMetadataReady(grpc_error_handle error) {
    if (error != GRPC_ERROR_NONE) {
      SetStatusFromError(recv_trailing_metadata_, error);
    }

    GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kForwarded);
    recv_trailing_state_ = RecvTrailingState::kComplete;

    ScopedContext context(this);
    WakeInsideCombiner();
  }

  void SetStatusFromError(TrailingMetadata* metadata, grpc_error_handle error) {
    grpc_status_code status_code = GRPC_STATUS_UNKNOWN;
    std::string status_details;
    grpc_error_get_status(error, deadline_, &status_code, &status_details,
                          nullptr, nullptr);
    metadata->Set(GrpcStatusMetadata(), status_code);
    metadata->Set(GrpcMessageMetadata(),
                  Slice::FromCopiedString(status_details));
  }

  void WakeInsideCombiner() {
    switch (send_initial_state_) {
      case SendInitialState::kQueued:
      case SendInitialState::kForwarded: {
        Poll<TrailingMetadata> poll = promise_();
        if (auto* r = absl::get_if<TrailingMetadata>(&poll)) {
          // TODO(ctiller): It's possible that a promise may decide to return
          // trailing metadata PRIOR to getting a recv_trailing_metadata op.
          // In that case we'll need to stash to metadata until such time as
          // we're ready to send it up.
          GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kComplete);
          *recv_trailing_metadata_ = std::move(*r);
          recv_trailing_state_ = RecvTrailingState::kResponded;
          grpc_closure* cb =
              absl::exchange(original_recv_trailing_metadata_ready_, nullptr);
          Closure::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
        }
      } break;
      case SendInitialState::kInitial:
      case SendInitialState::kCancelled:
        if (recv_trailing_state_ == RecvTrailingState::kComplete) {
          recv_trailing_state_ = RecvTrailingState::kResponded;
          grpc_closure* cb =
              absl::exchange(original_recv_trailing_metadata_ready_, nullptr);
          Closure::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
        }
        break;
    }
  }

  grpc_transport_stream_op_batch* send_initial_metadata_batch_ = nullptr;
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  grpc_closure recv_trailing_metadata_ready_;
  grpc_error_handle cancelled_error_ = GRPC_ERROR_NONE;
  SendInitialState send_initial_state_ = SendInitialState::kInitial;
  RecvTrailingState recv_trailing_state_ = RecvTrailingState::kInitial;
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
