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

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACE_H
#define GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACE_H

#include <grpc/impl/codegen/port_platform.h>

#include <grpc/grpc.h>
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {
namespace channelz {

class ChannelNode;

// Object used to hold live data for a channel. This data is exposed via the
// channelz service:
// https://github.com/grpc/proposal/blob/master/A14-channelz.md
class ChannelTrace {
 public:
  ChannelTrace(size_t max_events);
  ~ChannelTrace();

  enum Severity {
    Unset = 0,  // never to be used
    Info,       // we start at 1 to avoid using proto default values
    Warning,
    Error
  };

  // Adds a new trace event to the tracing object
  //
  // TODO(ncteisen): as this call is used more and more throughout the gRPC
  // stack, determine if it makes more sense to accept a char* instead of a
  // slice.
  void AddTraceEvent(Severity severity, grpc_slice data);

  // Adds a new trace event to the tracing object. This trace event refers to a
  // an event on a child of the channel. For example, if this channel has
  // created a new subchannel, then it would record that with a TraceEvent
  // referencing the new subchannel.
  //
  // TODO(ncteisen): as this call is used more and more throughout the gRPC
  // stack, determine if it makes more sense to accept a char* instead of a
  // slice.
  void AddTraceEventReferencingChannel(
      Severity severity, grpc_slice data,
      RefCountedPtr<ChannelNode> referenced_channel);
  void AddTraceEventReferencingSubchannel(
      Severity severity, grpc_slice data,
      RefCountedPtr<ChannelNode> referenced_subchannel);

  // Creates and returns the raw grpc_json object, so a parent channelz
  // object may incorporate the json before rendering.
  grpc_json* RenderJson() const;

 private:
  // Types of objects that can be references by trace events.
  enum class ReferencedType { Channel, Subchannel };
  // Private class to encapsulate all the data and bookkeeping needed for a
  // a trace event.
  class TraceEvent {
   public:
    // Constructor for a TraceEvent that references a different channel.
    TraceEvent(Severity severity, grpc_slice data,
               RefCountedPtr<ChannelNode> referenced_channel,
               ReferencedType type);

    // Constructor for a TraceEvent that does not reverence a different
    // channel.
    TraceEvent(Severity severity, grpc_slice data);

    ~TraceEvent();

    // Renders the data inside of this TraceEvent into a json object. This is
    // used by the ChannelTrace, when it is rendering itself.
    void RenderTraceEvent(grpc_json* json) const;

    // set and get for the next_ pointer.
    TraceEvent* next() const { return next_; }
    void set_next(TraceEvent* next) { next_ = next; }

   private:
    Severity severity_;
    grpc_slice data_;
    gpr_timespec timestamp_;
    TraceEvent* next_;
    // the tracer object for the (sub)channel that this trace event refers to.
    RefCountedPtr<ChannelNode> referenced_channel_;
    // the type that the referenced tracer points to. Unused if this trace
    // does not point to any channel or subchannel
    ReferencedType referenced_type_;
  };  // TraceEvent

  // Internal helper to add and link in a trace event
  void AddTraceEventHelper(TraceEvent* new_trace_event);

  gpr_mu tracer_mu_;
  uint64_t num_events_logged_;
  size_t list_size_;
  size_t max_list_size_;
  TraceEvent* head_trace_;
  TraceEvent* tail_trace_;
  gpr_timespec time_created_;
};

}  // namespace channelz
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACE_H */
