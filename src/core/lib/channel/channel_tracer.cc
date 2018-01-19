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
#include <grpc/support/useful.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/support/object_registry.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

grpc_core::DebugOnlyTraceFlag grpc_trace_channel_tracer_refcount(
    false, "channel_tracer_refcount");

// One node of tracing data
typedef struct grpc_trace_node {
  grpc_slice data;
  grpc_error* error;
  gpr_timespec time_created;
  grpc_connectivity_state connectivity_state;
  struct grpc_trace_node* next;

  // the tracer object for the (sub)channel that this trace node refers to.
  ChannelTracer* referenced_tracer;
} grpc_trace_node;

ChannelTracer::ChannelTracer(size_t max_nodes)
    : num_nodes_logged(0),
      list_size(0),
      max_list_size(max_nodes),
      head_trace(0),
      tail_trace(0) {
  gpr_mu_init(&tracer_mu);
  gpr_ref_init(&refs, 1);
  channel_uuid = grpc_object_registry_register_object(
      this, GRPC_OBJECT_REGISTRY_CHANNEL_TRACER);
  max_list_size = max_nodes;
  time_created = gpr_now(GPR_CLOCK_REALTIME);
}

ChannelTracer* ChannelTracer::Ref() {
  gpr_ref(&refs);
  return this;
}

static void free_node(grpc_trace_node* node) {
  GRPC_ERROR_UNREF(node->error);
  if (node->referenced_tracer) {
    node->referenced_tracer->Unref();
  }
  grpc_slice_unref_internal(node->data);
  gpr_free(node);
}

void ChannelTracer::Unref() {
  if (gpr_unref(&refs)) {
    grpc_trace_node* it = head_trace;
    while (it != nullptr) {
      grpc_trace_node* to_free = it;
      it = it->next;
      free_node(to_free);
    }
    gpr_mu_destroy(&tracer_mu);
  }
}

intptr_t ChannelTracer::GetUuid() { return channel_uuid; }

void ChannelTracer::AddTrace(grpc_slice data, grpc_error* error,
                             grpc_connectivity_state connectivity_state,
                             ChannelTracer* referenced_tracer) {
  ++num_nodes_logged;
  // create and fill up the new node
  grpc_trace_node* new_trace_node =
      static_cast<grpc_trace_node*>(gpr_malloc(sizeof(grpc_trace_node)));
  new_trace_node->data = data;
  new_trace_node->error = error;
  new_trace_node->time_created = gpr_now(GPR_CLOCK_REALTIME);
  new_trace_node->connectivity_state = connectivity_state;
  new_trace_node->next = nullptr;
  new_trace_node->referenced_tracer =
      (referenced_tracer) ? referenced_tracer->Ref() : nullptr;
  // first node case
  if (head_trace == nullptr) {
    head_trace = tail_trace = new_trace_node;
  }
  // regular node add case
  else {
    tail_trace->next = new_trace_node;
    tail_trace = tail_trace->next;
  }
  ++list_size;
  // maybe garbage collect the end
  if (list_size > max_list_size) {
    grpc_trace_node* to_free = head_trace;
    head_trace = head_trace->next;
    free_node(to_free);
    --list_size;
  }
}

// returns an allocated string that represents tm according to RFC-3339.
static char* fmt_time(gpr_timespec tm) {
  char buffer[35];
  struct tm* tm_info = localtime((const time_t*)&tm.tv_sec);
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", tm_info);
  char* full_time_str;
  gpr_asprintf(&full_time_str, "%s.%09dZ", buffer, tm.tv_nsec);
  return full_time_str;
}

class ChannelTracerRenderer {
 public:
  ChannelTracerRenderer(ChannelTracer* tracer, bool recursive)
      : current_tracer_(tracer),
        recursive_(recursive),
        seen_tracers_(nullptr),
        size_(0),
        cap_(0) {}

  char* Run() {
    grpc_json* json = grpc_json_create(GRPC_JSON_OBJECT);
    AddSeenTracer(current_tracer_);
    RecursivelyPopulateJson(json);
    gpr_free(seen_tracers_);
    char* json_str = grpc_json_dump_to_string(json, 1);
    grpc_json_destroy(json);
    return json_str;
  }

 private:
  void AddSeenTracer(ChannelTracer* newly_seen) {
    if (size_ >= cap_) {
      cap_ = GPR_MAX(5 * sizeof(newly_seen), 3 * cap_ / 2);
      seen_tracers_ = (ChannelTracer**)gpr_realloc(seen_tracers_, cap_);
    }
    seen_tracers_[size_++] = newly_seen;
  }

  bool TracerAlreadySeen(ChannelTracer* tracer) {
    for (size_t i = 0; i < size_; ++i) {
      if (seen_tracers_[i] == tracer) return true;
    }
    return false;
  }

  void RecursivelyPopulateJson(grpc_json* json) {
    grpc_json* channel_data = grpc_json_create_child(
        nullptr, json, "channelData", nullptr, GRPC_JSON_OBJECT, false);
    grpc_json* children = nullptr;
    if (recursive_) {
      children = grpc_json_create_child(channel_data, json, "children", nullptr,
                                        GRPC_JSON_ARRAY, false);
    }
    PopulateTracer(channel_data, children);
  }

  void PopulateTracer(grpc_json* channel_data, grpc_json* children) {
    grpc_json* child = nullptr;

    char* uuid_str;
    gpr_asprintf(&uuid_str, "%" PRIdPTR, current_tracer_->channel_uuid);
    child = grpc_json_create_child(child, channel_data, "uuid", uuid_str,
                                   GRPC_JSON_NUMBER, true);
    char* num_nodes_logged_str;
    gpr_asprintf(&num_nodes_logged_str, "%" PRId64,
                 current_tracer_->num_nodes_logged);
    child =
        grpc_json_create_child(child, channel_data, "numNodesLogged",
                               num_nodes_logged_str, GRPC_JSON_NUMBER, true);
    child = grpc_json_create_child(child, channel_data, "startTime",
                                   fmt_time(current_tracer_->time_created),
                                   GRPC_JSON_STRING, true);
    child = grpc_json_create_child(child, channel_data, "nodes", nullptr,
                                   GRPC_JSON_ARRAY, false);
    PopulateNodeList(child, children);
  }

  void PopulateNodeList(grpc_json* nodes, grpc_json* children) {
    grpc_json* child = nullptr;
    grpc_trace_node* it = current_tracer_->head_trace;
    while (it != nullptr) {
      child = grpc_json_create_child(child, nodes, nullptr, nullptr,
                                     GRPC_JSON_OBJECT, false);
      PopulateNode(it, child, children);
      it = it->next;
    }
  }

  void PopulateNode(grpc_trace_node* node, grpc_json* json,
                    grpc_json* children) {
    grpc_json* child = nullptr;
    child = grpc_json_create_child(child, json, "data",
                                   grpc_slice_to_c_string(node->data),
                                   GRPC_JSON_STRING, true);
    if (node->error != GRPC_ERROR_NONE) {
      child = grpc_json_create_child(child, json, "error",
                                     gpr_strdup(grpc_error_string(node->error)),
                                     GRPC_JSON_STRING, true);
    }
    child = grpc_json_create_child(child, json, "time",
                                   fmt_time(node->time_created),
                                   GRPC_JSON_STRING, true);
    child = grpc_json_create_child(
        child, json, "state",
        grpc_connectivity_state_name(node->connectivity_state),
        GRPC_JSON_STRING, false);
    if (node->referenced_tracer != nullptr) {
      char* uuid_str;
      gpr_asprintf(&uuid_str, "%" PRIdPTR,
                   node->referenced_tracer->channel_uuid);
      child = grpc_json_create_child(child, json, "uuid", uuid_str,
                                     GRPC_JSON_NUMBER, true);
      if (children && !TracerAlreadySeen(node->referenced_tracer)) {
        grpc_json* referenced_tracer = grpc_json_create_child(
            nullptr, children, nullptr, nullptr, GRPC_JSON_OBJECT, false);
        AddSeenTracer(node->referenced_tracer);
        ChannelTracer* saved = current_tracer_;
        current_tracer_ = node->referenced_tracer;
        RecursivelyPopulateJson(referenced_tracer);
        current_tracer_ = saved;
      }
    }
  }

  ChannelTracer* current_tracer_;
  bool recursive_;
  ChannelTracer** seen_tracers_;
  size_t size_;
  size_t cap_;
};

char* ChannelTracer::RenderTrace(bool recursive) {
  ChannelTracerRenderer renderer(this, recursive);
  return renderer.Run();
}

char* ChannelTracer::GetChannelTraceFromUuid(intptr_t uuid, bool recursive) {
  void* object;
  grpc_object_registry_type type =
      grpc_object_registry_get_object(uuid, &object);
  GPR_ASSERT(type == GRPC_OBJECT_REGISTRY_CHANNEL_TRACER);
  return static_cast<ChannelTracer*>(object)->RenderTrace(recursive);
}

}  // namespace grpc_core
