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

#include "src/core/lib/channel/channel_tracer.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/lib/channel/object_registry.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

class TraceEvent {
 public:
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

  TraceEvent(grpc_slice data, grpc_error* error,
             grpc_connectivity_state connectivity_state)
      : data_(data),
        error_(error),
        connectivity_state_(connectivity_state),
        next_(nullptr),
        referenced_tracer_(nullptr) {
    time_created_ = gpr_now(GPR_CLOCK_REALTIME);
  }

  ~TraceEvent() {
    GRPC_ERROR_UNREF(error_);
    referenced_tracer_.reset();
    grpc_slice_unref_internal(data_);
  }

 private:
  friend class ChannelTrace;
  friend class ChannelTraceRenderer;
  grpc_slice data_;
  grpc_error* error_;
  gpr_timespec time_created_;
  grpc_connectivity_state connectivity_state_;
  TraceEvent* next_;
  // the tracer object for the (sub)channel that this trace event refers to.
  RefCountedPtr<ChannelTrace> referenced_tracer_;
};

ChannelTrace::ChannelTrace(size_t max_events)
    : channel_uuid_(-1),
      num_events_logged_(0),
      num_children_seen_(0),
      list_size_(0),
      max_list_size_(max_events),
      head_trace_(nullptr),
      tail_trace_(nullptr) {
  if (max_list_size_ == 0) return;  // tracing is disabled if max_events == 0
  gpr_mu_init(&tracer_mu_);
  channel_uuid_ = grpc_object_registry_register_object(
      this, GRPC_OBJECT_REGISTRY_CHANNEL_TRACER);
  time_created_ = gpr_now(GPR_CLOCK_REALTIME);
}

ChannelTrace::~ChannelTrace() {
  if (max_list_size_ == 0) return;  // tracing is disabled if max_events == 0
  TraceEvent* it = head_trace_;
  while (it != nullptr) {
    TraceEvent* to_free = it;
    it = it->next_;
    gpr_free(to_free);
  }
  gpr_mu_destroy(&tracer_mu_);
}

intptr_t ChannelTrace::GetUuid() { return channel_uuid_; }

void ChannelTrace::AddTraceEventHelper(TraceEvent* new_trace_event) {
  ++num_events_logged_;
  // first event case
  if (head_trace_ == nullptr) {
    head_trace_ = tail_trace_ = new_trace_event;
  }
  // regular event add case
  else {
    tail_trace_->next_ = new_trace_event;
    tail_trace_ = tail_trace_->next_;
  }
  ++list_size_;
  // maybe garbage collect the end
  if (list_size_ > max_list_size_) {
    TraceEvent* to_free = head_trace_;
    head_trace_ = head_trace_->next_;
    gpr_free(to_free);
    --list_size_;
  }
}

void ChannelTrace::AddTraceEvent(
    grpc_slice data, grpc_error* error,
    grpc_connectivity_state connectivity_state,
    RefCountedPtr<ChannelTrace> referenced_tracer) {
  if (max_list_size_ == 0) return;  // tracing is disabled if max_events == 0
  ++num_children_seen_;
  // create and fill up the new event
  AddTraceEventHelper(New<TraceEvent>(data, error, connectivity_state,
                                      std::move(referenced_tracer)));
}

void ChannelTrace::AddTraceEvent(grpc_slice data, grpc_error* error,
                                 grpc_connectivity_state connectivity_state) {
  AddTraceEventHelper(New<TraceEvent>(data, error, connectivity_state));
}

namespace {

// returns an allocated string that represents tm according to RFC-3339.
char* fmt_time(gpr_timespec tm) {
  char buffer[35];
  struct tm* tm_info = localtime((const time_t*)&tm.tv_sec);
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", tm_info);
  char* full_time_str;
  gpr_asprintf(&full_time_str, "%s.%09dZ", buffer, tm.tv_nsec);
  return full_time_str;
}

}  // anonymous namespace

class ChannelTraceRenderer {
 public:
  // If recursive==true, then the entire tree of trace will be rendered.
  // If not, then only the top level data will be.
  ChannelTraceRenderer(ChannelTrace* tracer, bool recursive)
      : current_tracer_(tracer),
        recursive_(recursive),
        seen_tracers_(nullptr),
        size_(0),
        cap_(0) {}

  // Renders the trace and returns an allocated char* with the formatted JSON
  char* Run() {
    grpc_json* json = grpc_json_create(GRPC_JSON_OBJECT);
    AddSeenTracer(current_tracer_);
    RecursivelyPopulateJson(json);
    gpr_free(seen_tracers_);
    char* json_str = grpc_json_dump_to_string(json, 0);
    grpc_json_destroy(json);
    return json_str;
  }

 private:
  // tracks that a tracer has already been rendered to avoid infinite
  // recursion.
  void AddSeenTracer(ChannelTrace* newly_seen) {
    if (size_ >= cap_) {
      cap_ = GPR_MAX(5 * sizeof(newly_seen), 3 * cap_ / 2);
      seen_tracers_ = (ChannelTrace**)gpr_realloc(seen_tracers_, cap_);
    }
    seen_tracers_[size_++] = newly_seen;
  }

  // Checks if a tracer has already been seen.
  bool TracerAlreadySeen(ChannelTrace* tracer) {
    for (size_t i = 0; i < size_; ++i) {
      if (seen_tracers_[i] == tracer) return true;
    }
    return false;
  }

  // Recursively fills up json by walking over all of the trace of
  // current_tracer_. Starts at the top level, by creating the fields
  // channelData, and childData.
  void RecursivelyPopulateJson(grpc_json* json) {
    grpc_json* channel_data = grpc_json_create_child(
        nullptr, json, "channelData", nullptr, GRPC_JSON_OBJECT, false);
    grpc_json* children = nullptr;
    if (recursive_) {
      children = grpc_json_create_child(channel_data, json, "childData",
                                        nullptr, GRPC_JSON_ARRAY, false);
    }
    PopulateChannelData(channel_data, children);
  }

  // Fills up the channelData object. If children is not null, it will
  // recursively populate each referenced child as it passes that event.
  void PopulateChannelData(grpc_json* channel_data, grpc_json* children) {
    grpc_json* child = nullptr;
    char* uuid_str;
    gpr_asprintf(&uuid_str, "%" PRIdPTR, current_tracer_->channel_uuid_);
    child = grpc_json_create_child(child, channel_data, "uuid", uuid_str,
                                   GRPC_JSON_NUMBER, true);
    char* num_events_logged_str;
    gpr_asprintf(&num_events_logged_str, "%" PRId64,
                 current_tracer_->num_events_logged_);
    child =
        grpc_json_create_child(child, channel_data, "numNodesLogged",
                               num_events_logged_str, GRPC_JSON_NUMBER, true);
    child = grpc_json_create_child(child, channel_data, "startTime",
                                   fmt_time(current_tracer_->time_created_),
                                   GRPC_JSON_STRING, true);
    child = grpc_json_create_child(child, channel_data, "nodes", nullptr,
                                   GRPC_JSON_ARRAY, false);
    PopulateNodeList(child, children);
  }

  // Iterated over the list of TraceEvents and populates their data.
  void PopulateNodeList(grpc_json* nodes, grpc_json* children) {
    grpc_json* child = nullptr;
    TraceEvent* it = current_tracer_->head_trace_;
    while (it != nullptr) {
      child = grpc_json_create_child(child, nodes, nullptr, nullptr,
                                     GRPC_JSON_OBJECT, false);
      PopulateNode(it, child, children);
      it = it->next_;
    }
  }

  // Fills in all the data for a single TraceEvent. If children is not null
  // and the TraceEvent refers to a child Tracer object and recursive_ is true,
  // then that child object will be rendered into the trace.
  void PopulateNode(TraceEvent* node, grpc_json* json, grpc_json* children) {
    grpc_json* child = nullptr;
    child = grpc_json_create_child(child, json, "data",
                                   grpc_slice_to_c_string(node->data_),
                                   GRPC_JSON_STRING, true);
    if (node->error_ != GRPC_ERROR_NONE) {
      child = grpc_json_create_child(
          child, json, "error", gpr_strdup(grpc_error_string(node->error_)),
          GRPC_JSON_STRING, true);
    }
    child = grpc_json_create_child(child, json, "time",
                                   fmt_time(node->time_created_),
                                   GRPC_JSON_STRING, true);
    child = grpc_json_create_child(
        child, json, "state",
        grpc_connectivity_state_name(node->connectivity_state_),
        GRPC_JSON_STRING, false);
    if (node->referenced_tracer_ != nullptr) {
      char* uuid_str;
      gpr_asprintf(&uuid_str, "%" PRIdPTR,
                   node->referenced_tracer_->channel_uuid_);
      child = grpc_json_create_child(child, json, "uuid", uuid_str,
                                     GRPC_JSON_NUMBER, true);

      // If we are recursively populating everything, and this node
      // references a tracer we haven't seen yet, we render that tracer
      // in full, adding it to the parent JSON's "children" field.
      if (children && !TracerAlreadySeen(node->referenced_tracer_.get())) {
        grpc_json* referenced_tracer = grpc_json_create_child(
            nullptr, children, nullptr, nullptr, GRPC_JSON_OBJECT, false);
        AddSeenTracer(node->referenced_tracer_.get());
        ChannelTrace* saved = current_tracer_;
        current_tracer_ = node->referenced_tracer_.get();
        RecursivelyPopulateJson(referenced_tracer);
        current_tracer_ = saved;
      }
    }
  }

  // Tracks the current tracer we are rendering as we walk the tree of tracers.
  ChannelTrace* current_tracer_;

  // If true, we will render the data of all of this tracer's children.
  bool recursive_;

  // These members are used to track tracers we have already rendered. This is
  // a dynamically growing array that is deallocated when the rendering is done.
  ChannelTrace** seen_tracers_;
  size_t size_;
  size_t cap_;
};

char* ChannelTrace::RenderTrace(bool recursive) {
  if (!max_list_size_)
    return nullptr;  // tracing is disabled if max_events == 0
  ChannelTraceRenderer renderer(this, recursive);
  return renderer.Run();
}

char* ChannelTrace::GetChannelTraceFromUuid(intptr_t uuid, bool recursive) {
  void* object;
  grpc_object_registry_type type =
      grpc_object_registry_get_object(uuid, &object);
  GPR_ASSERT(type == GRPC_OBJECT_REGISTRY_CHANNEL_TRACER);
  return static_cast<ChannelTrace*>(object)->RenderTrace(recursive);
}

}  // namespace grpc_core
