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

#include "src/core/lib/channel/channel_trace.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {
namespace channelz {

ChannelTrace::TraceEvent::TraceEvent(Severity severity, grpc_slice data,
                                     RefCountedPtr<BaseNode> referenced_entity)
    : severity_(severity),
      data_(data),
      timestamp_(grpc_millis_to_timespec(grpc_core::ExecCtx::Get()->Now(),
                                         GPR_CLOCK_REALTIME)),
      next_(nullptr),
      referenced_entity_(std::move(referenced_entity)),
      memory_usage_(sizeof(TraceEvent) + grpc_slice_memory_usage(data)) {}

ChannelTrace::TraceEvent::TraceEvent(Severity severity, grpc_slice data)
    : severity_(severity),
      data_(data),
      timestamp_(grpc_millis_to_timespec(grpc_core::ExecCtx::Get()->Now(),
                                         GPR_CLOCK_REALTIME)),
      next_(nullptr),
      memory_usage_(sizeof(TraceEvent) + grpc_slice_memory_usage(data)) {}

ChannelTrace::TraceEvent::~TraceEvent() { grpc_slice_unref_internal(data_); }

ChannelTrace::ChannelTrace(size_t max_event_memory)
    : num_events_logged_(0),
      event_list_memory_usage_(0),
      max_event_memory_(max_event_memory),
      head_trace_(nullptr),
      tail_trace_(nullptr) {
  if (max_event_memory_ == 0)
    return;  // tracing is disabled if max_event_memory_ == 0
  gpr_mu_init(&tracer_mu_);
  time_created_ = grpc_millis_to_timespec(grpc_core::ExecCtx::Get()->Now(),
                                          GPR_CLOCK_REALTIME);
}

ChannelTrace::~ChannelTrace() {
  if (max_event_memory_ == 0)
    return;  // tracing is disabled if max_event_memory_ == 0
  TraceEvent* it = head_trace_;
  while (it != nullptr) {
    TraceEvent* to_free = it;
    it = it->next();
    Delete<TraceEvent>(to_free);
  }
  gpr_mu_destroy(&tracer_mu_);
}

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
  event_list_memory_usage_ += new_trace_event->memory_usage();
  // maybe garbage collect the tail until we are under the memory limit.
  while (event_list_memory_usage_ > max_event_memory_) {
    TraceEvent* to_free = head_trace_;
    event_list_memory_usage_ -= to_free->memory_usage();
    head_trace_ = head_trace_->next();
    Delete<TraceEvent>(to_free);
  }
}

void ChannelTrace::AddTraceEvent(Severity severity, grpc_slice data) {
  if (max_event_memory_ == 0) {
    grpc_slice_unref_internal(data);
    return;  // tracing is disabled if max_event_memory_ == 0
  }
  AddTraceEventHelper(New<TraceEvent>(severity, data));
}

void ChannelTrace::AddTraceEventWithReference(
    Severity severity, grpc_slice data,
    RefCountedPtr<BaseNode> referenced_entity) {
  if (max_event_memory_ == 0) {
    grpc_slice_unref_internal(data);
    return;  // tracing is disabled if max_event_memory_ == 0
  }
  // create and fill up the new event
  AddTraceEventHelper(
      New<TraceEvent>(severity, data, std::move(referenced_entity)));
}

namespace {

const char* severity_string(ChannelTrace::Severity severity) {
  switch (severity) {
    case ChannelTrace::Severity::Info:
      return "CT_INFO";
    case ChannelTrace::Severity::Warning:
      return "CT_WARNING";
    case ChannelTrace::Severity::Error:
      return "CT_ERROR";
    default:
      GPR_UNREACHABLE_CODE(return "CT_UNKNOWN");
  }
}

}  // anonymous namespace

void ChannelTrace::TraceEvent::RenderTraceEvent(grpc_json* json) const {
  grpc_json* json_iterator = nullptr;
  json_iterator = grpc_json_create_child(json_iterator, json, "description",
                                         grpc_slice_to_c_string(data_),
                                         GRPC_JSON_STRING, true);
  json_iterator = grpc_json_create_child(json_iterator, json, "severity",
                                         severity_string(severity_),
                                         GRPC_JSON_STRING, false);
  json_iterator = grpc_json_create_child(json_iterator, json, "timestamp",
                                         gpr_format_timespec(timestamp_),
                                         GRPC_JSON_STRING, true);
  if (referenced_entity_ != nullptr) {
    const bool is_channel =
        (referenced_entity_->type() == BaseNode::EntityType::kTopLevelChannel ||
         referenced_entity_->type() == BaseNode::EntityType::kInternalChannel);
    char* uuid_str;
    gpr_asprintf(&uuid_str, "%" PRIdPTR, referenced_entity_->uuid());
    grpc_json* child_ref = grpc_json_create_child(
        json_iterator, json, is_channel ? "channelRef" : "subchannelRef",
        nullptr, GRPC_JSON_OBJECT, false);
    json_iterator = grpc_json_create_child(
        nullptr, child_ref, is_channel ? "channelId" : "subchannelId", uuid_str,
        GRPC_JSON_STRING, true);
    json_iterator = child_ref;
  }
}

grpc_json* ChannelTrace::RenderJson() const {
  if (max_event_memory_ == 0)
    return nullptr;  // tracing is disabled if max_event_memory_ == 0
  grpc_json* json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json_iterator = nullptr;
  if (num_events_logged_ > 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "numEventsLogged", num_events_logged_);
  }
  json_iterator = grpc_json_create_child(
      json_iterator, json, "creationTimestamp",
      gpr_format_timespec(time_created_), GRPC_JSON_STRING, true);
  // only add in the event list if it is non-empty.
  if (head_trace_ != nullptr) {
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
  }
  return json;
}

}  // namespace channelz
}  // namespace grpc_core
