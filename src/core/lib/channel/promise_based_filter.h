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
      : arena_(args->arena) {}

 protected:
  class ScopedContext : public promise_detail::Context<Arena> {
   public:
    explicit ScopedContext(BaseCallData* call_data)
        : promise_detail::Context<Arena>(call_data->arena_) {}
  };

  Arena* const arena_;
  ArenaPromise<TrailingMetadata> promise_ =
      ArenaPromise<TrailingMetadata>(Never<TrailingMetadata>());
};

template <class ChannelFilter, bool kIsClient = ChannelFilter::is_client()>
class CallData;

template <class ChannelFilter>
class CallData<ChannelFilter, true> : public BaseCallData {
 public:
  explicit CallData(const grpc_call_element_args* args) : BaseCallData(args) {
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadata,
                      this, grpc_schedule_on_exec_ctx);
  }

  void Op(grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
    ScopedContext context(this);
    ChannelFilter* filter = static_cast<ChannelFilter*>(elem->channel_data);
    if (op->recv_trailing_metadata) {
      recv_trailing_metadata_ =
          op->payload->recv_trailing_metadata.recv_trailing_metadata;
      original_recv_trailing_metadata_ready_ =
          op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
      op->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
          &recv_trailing_metadata_ready_;
      if (!op->send_initial_metadata) {
        WakeInsideCombiner();
      }
    }
    if (op->send_initial_metadata) {
      GPR_ASSERT(send_initial_metadata_op_ == nullptr);
      send_initial_metadata_op_ = op;
      promise_ = filter->MakeCallPromise(
          op->payload->send_initial_metadata.send_initial_metadata,
          [elem](InitialMetadata* initial_metadata) {
            CallData* self = static_cast<CallData*>(elem->call_data);
            self->send_initial_metadata_op_->payload->send_initial_metadata
                .send_initial_metadata = initial_metadata;
            return ArenaPromise<TrailingMetadata>(
                [elem]() -> Poll<TrailingMetadata> {
                  CallData* self = static_cast<CallData*>(elem->call_data);
                  if (grpc_transport_stream_op_batch* op = absl::exchange(
                          self->send_initial_metadata_op_, nullptr)) {
                    // First poll: call next filter.
                    grpc_call_next_op(elem, op);
                  }
                  if (self->recieved_trailing_metadata_) {
                    return std::move(*self->recv_trailing_metadata_);
                  }
                  return Pending{};
                });
          });
      WakeInsideCombiner();
      return;
    }
    if (op->cancel_stream) {
      promise_ =
          ArenaPromise<TrailingMetadata>([elem]() -> Poll<TrailingMetadata> {
            CallData* self = static_cast<CallData*>(elem->call_data);
            if (self->recieved_trailing_metadata_) {
              return std::move(*self->recv_trailing_metadata_);
            }
          });
    }
    grpc_call_next_op(elem, op);
  }

 private:
  static void RecvTrailingMetadata(void* arg, grpc_error_handle error) {
    CallData* self = static_cast<CallData*>(arg);
    ScopedContext context(self);
    GPR_ASSERT(!self->recieved_trailing_metadata_);
    self->recieved_trailing_metadata_ = true;
    self->WakeInsideCombiner();
  }

  void WakeInsideCombiner() {
    if (original_recv_trailing_metadata_ready_ != nullptr) {
      Poll<TrailingMetadata> poll = promise_();
      if (auto* r = absl::get_if<TrailingMetadata>(&poll)) {
        *recv_trailing_metadata_ = std::move(*r);
        grpc_closure* cb =
            absl::exchange(original_recv_trailing_metadata_ready_, nullptr);
        Closure::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
      }
    }
  }

  grpc_transport_stream_op_batch* send_initial_metadata_op_ = nullptr;
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  grpc_closure recv_trailing_metadata_ready_;
  bool recieved_trailing_metadata_ = false;
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
