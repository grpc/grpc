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

#include <grpc/impl/codegen/port_platform.h>

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

ChannelTrace::TraceEvent::~TraceEvent() {
  GRPC_ERROR_UNREF(error_);
  referenced_tracer_.reset();
  grpc_slice_unref_internal(data_);
}

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
    it = it->next();
    Delete<TraceEvent>(to_free);
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
    tail_trace_->set_next(new_trace_event);
    tail_trace_ = tail_trace_->next();
  }
  ++list_size_;
  // maybe garbage collect the end
  if (list_size_ > max_list_size_) {
    TraceEvent* to_free = head_trace_;
    head_trace_ = head_trace_->next();
    Delete<TraceEvent>(to_free);
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

void ChannelTrace::TraceEvent::RenderTraceEvent(grpc_json* json) {
  grpc_json* json_iterator = nullptr;
  json_iterator = grpc_json_create_child(json_iterator, json, "description",
                                         grpc_slice_to_c_string(data_),
                                         GRPC_JSON_STRING, true);
  // TODO(ncteisen): Either format this as google.rpc.Status here, or ensure
  // it is done in the layers above core.
  if (error_ != GRPC_ERROR_NONE) {
    json_iterator = grpc_json_create_child(
        json_iterator, json, "status", gpr_strdup(grpc_error_string(error_)),
        GRPC_JSON_STRING, true);
  }
  json_iterator =
      grpc_json_create_child(json_iterator, json, "timestamp",
                             fmt_time(time_created_), GRPC_JSON_STRING, true);
  json_iterator =
      grpc_json_create_child(json_iterator, json, "state",
                             grpc_connectivity_state_name(connectivity_state_),
                             GRPC_JSON_STRING, false);
  if (referenced_tracer_ != nullptr) {
    char* uuid_str;
    gpr_asprintf(&uuid_str, "%" PRIdPTR, referenced_tracer_->channel_uuid_);
    json_iterator = grpc_json_create_child(json_iterator, json, "child_ref",
                                           uuid_str, GRPC_JSON_NUMBER, true);
  }
}

char* ChannelTrace::RenderTrace() {
  if (!max_list_size_)
    return nullptr;  // tracing is disabled if max_events == 0
  grpc_json* json = grpc_json_create(GRPC_JSON_OBJECT);
  char* num_events_logged_str;
  gpr_asprintf(&num_events_logged_str, "%" PRId64, num_events_logged_);
  grpc_json* json_iterator = nullptr;
  json_iterator =
      grpc_json_create_child(json_iterator, json, "num_events_logged",
                             num_events_logged_str, GRPC_JSON_NUMBER, true);
  json_iterator =
      grpc_json_create_child(json_iterator, json, "creation_time",
                             fmt_time(time_created_), GRPC_JSON_STRING, true);
  grpc_json* events = grpc_json_create_child(json_iterator, json, "events",
                                             nullptr, GRPC_JSON_ARRAY, false);
  json_iterator = nullptr;
  TraceEvent* it = head_trace_;
  while (it != nullptr) {
    json_iterator = grpc_json_create_child(json_iterator, events, nullptr,
                                           nullptr, GRPC_JSON_OBJECT, false);
    it->RenderTraceEvent(json_iterator);
    it = it->next();
  }
  char* json_str = grpc_json_dump_to_string(json, 0);
  grpc_json_destroy(json);
  return json_str;
}

}  // namespace grpc_core
