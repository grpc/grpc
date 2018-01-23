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

class ChannelTracer : public RefCounted {
 public:
  ChannelTracer(size_t max_nodes);
  ~ChannelTracer();

  /* returns the tracers uuid */
  intptr_t GetUuid();

  /* Adds a new trace node to the tracing object */
  void AddTrace(grpc_slice data, grpc_error* error,
                grpc_connectivity_state connectivity_state);

  /* Adds a new trace node to the tracing object. This trace node refers to a
     an event on a child of the channel. For example this could log when a
     particular subchannel becomes connected */
  void AddTrace(grpc_slice data, grpc_error* error,
                grpc_connectivity_state connectivity_state,
                RefCountedPtr<ChannelTracer> referenced_tracer);

  /* Returns the tracing data rendered as a grpc json string.
     The string is owned by the caller and must be freed. If recursive
     is true, then the string will include the recursive trace for all
     subtracing objects. */
  char* RenderTrace(bool recursive);

  /* util functions that perform the uuid -> tracer step for you, and then
     returns the trace for the uuid given. */
  static char* GetChannelTraceFromUuid(intptr_t uuid, bool recursive);

 private:
  // Internal helper that frees a TraceEvent.
  void FreeNode(TraceEvent* node);

  // Internal helper to add and link in a tracenode
  void AddTraceEventNode(TraceEvent* new_trace_node);

  friend class ChannelTracerRenderer;
  gpr_mu tracer_mu_;
  intptr_t channel_uuid_;
  uint64_t num_nodes_logged_;
  uint64_t num_children_seen_;
  size_t list_size_;
  size_t max_list_size_;
  TraceEvent* head_trace_;
  TraceEvent* tail_trace_;
  gpr_timespec time_created_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACER_H */
