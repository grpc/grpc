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

#include <grpc/grpc.h>
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

class TraceEvent;

class ChannelTrace : public RefCounted<ChannelTrace> {
 public:
  ChannelTrace(size_t max_events);
  ~ChannelTrace();

  /* returns the tracer's uuid */
  intptr_t GetUuid();

  /* Adds a new trace event to the tracing object */
  void AddTraceEvent(grpc_slice data, grpc_error* error,
                     grpc_connectivity_state connectivity_state);

  /* Adds a new trace event to the tracing object. This trace event refers to a
     an event on a child of the channel. For example this could log when a
     particular subchannel becomes connected.
     TODO(ncteisen): Once channelz is implemented, the events should reference
     the channelz object, not the channel trace. */
  void AddTraceEvent(grpc_slice data, grpc_error* error,
                     grpc_connectivity_state connectivity_state,
                     RefCountedPtr<ChannelTrace> referenced_tracer);

  /* Returns the tracing data rendered as a grpc json string.
     The string is owned by the caller and must be freed. If recursive
     is true, then the string will include the recursive trace for all
     subtracing objects. */
  char* RenderTrace(bool recursive);

 private:
  // Internal helper to add and link in a trace event
  void AddTraceEventHelper(TraceEvent* new_trace_event);

  friend class ChannelTraceRenderer;
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
