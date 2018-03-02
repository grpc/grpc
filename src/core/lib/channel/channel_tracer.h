/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACER_H
#define GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACER_H

#include <grpc/impl/codegen/port_platform.h>

#include <grpc/grpc.h>
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

// Object used to hold live data for a channel. This data is exposed via the
// channelz service:
// https://github.com/grpc/proposal/blob/master/A14-channelz.md
class ChannelTrace : public RefCounted<ChannelTrace> {
 public:
  ChannelTrace(size_t max_events);
  ~ChannelTrace();

  // returns the tracer's uuid
  intptr_t GetUuid();

  // Adds a new trace event to the tracing object
  void AddTraceEvent(grpc_slice data, grpc_error* error,
                     grpc_connectivity_state connectivity_state);

  // Adds a new trace event to the tracing object. This trace event refers to a
  // an event on a child of the channel. For example, if this channel has
  // created a new subchannel, then it would record that with a TraceEvent
  // referencing the new subchannel.

  // TODO(ncteisen): Once channelz is implemented, the events should reference
  // the overall channelz object, not just the ChannelTrace object.
  void AddTraceEvent(grpc_slice data, grpc_error* error,
                     grpc_connectivity_state connectivity_state,
                     RefCountedPtr<ChannelTrace> referenced_tracer);

  // Returns the tracing data rendered as a grpc json string.
  // The string is owned by the caller and must be freed.
  char* RenderTrace();

 private:
  // Private class to encapsulate all the data and bookkeeping needed for a
  // a trace event.
  class TraceEvent {
   public:
    // Constructor for a TraceEvent that references a different channel.
    // TODO(ncteisen): once channelz is implemented, this should reference the
    // overall channelz object, not just the ChannelTrace object
    TraceEvent(grpc_slice data, grpc_error* error,
               grpc_connectivity_state connectivity_state,
               RefCountedPtr<ChannelTrace> referenced_tracer)
        : data_(data),
          error_(error),
          connectivity_state_(connectivity_state),
          next_(nullptr),
          referenced_tracer_(std::move(referenced_tracer)) {
      time_created_ = gpr_now(GPR_CLOCK_REALTIME);
    }

    // Constructor for a TraceEvent that does not reverence a different
    // channel.
    TraceEvent(grpc_slice data, grpc_error* error,
               grpc_connectivity_state connectivity_state)
        : data_(data),
          error_(error),
          connectivity_state_(connectivity_state),
          next_(nullptr),
          referenced_tracer_(nullptr) {
      time_created_ = gpr_now(GPR_CLOCK_REALTIME);
    }

    ~TraceEvent();

    // Renders the data inside of this TraceEvent into a json object. This is
    // used by the ChannelTrace, when it is rendering itself.
    void RenderTraceEvent(grpc_json* json);

    // set and get for the next_ pointer.
    TraceEvent* next() { return next_; }
    void set_next(TraceEvent* next) { next_ = next; }

   private:
    grpc_slice data_;
    grpc_error* error_;
    gpr_timespec time_created_;
    grpc_connectivity_state connectivity_state_;
    TraceEvent* next_;
    // the tracer object for the (sub)channel that this trace event refers to.
    RefCountedPtr<ChannelTrace> referenced_tracer_;
  };  // TraceEvent

  // Internal helper to add and link in a trace event
  void AddTraceEventHelper(TraceEvent* new_trace_event);

  gpr_mu tracer_mu_;
  intptr_t channel_uuid_;
  uint64_t num_events_logged_;
  uint64_t num_children_seen_;
  size_t list_size_;
  size_t max_list_size_;
  TraceEvent* head_trace_;
  TraceEvent* tail_trace_;
  gpr_timespec time_created_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACER_H */
