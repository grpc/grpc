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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

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

ChannelTrace::TraceEvent::TraceEvent(Severity severity, const grpc_slice& data,
                                     RefCountedPtr<BaseNode> referenced_entity)
    : severity_(severity),
      data_(data),
      timestamp_(
          grpc_millis_to_timespec(ExecCtx::Get()->Now(), GPR_CLOCK_REALTIME)),
      next_(nullptr),
      referenced_entity_(std::move(referenced_entity)),
      memory_usage_(sizeof(TraceEvent) + grpc_slice_memory_usage(data)) {}

ChannelTrace::TraceEvent::TraceEvent(Severity severity, const grpc_slice& data)
    : severity_(severity),
      data_(data),
      timestamp_(
          grpc_millis_to_timespec(ExecCtx::Get()->Now(), GPR_CLOCK_REALTIME)),
      next_(nullptr),
      memory_usage_(sizeof(TraceEvent) + grpc_slice_memory_usage(data)) {}

ChannelTrace::TraceEvent::~TraceEvent() { grpc_slice_unref_internal(data_); }

ChannelTrace::ChannelTrace(size_t max_event_memory)
    : num_events_logged_(0),
      event_list_memory_usage_(0),
      max_event_memory_(max_event_memory),
      head_trace_(nullptr),
      tail_trace_(nullptr) {
  if (max_event_memory_ == 0) {
    return;  // tracing is disabled if max_event_memory_ == 0
  }
  gpr_mu_init(&tracer_mu_);
  time_created_ =
      grpc_millis_to_timespec(ExecCtx::Get()->Now(), GPR_CLOCK_REALTIME);
}

ChannelTrace::~ChannelTrace() {
  if (max_event_memory_ == 0) {
    return;  // tracing is disabled if max_event_memory_ == 0
  }
  TraceEvent* it = head_trace_;
  while (it != nullptr) {
    TraceEvent* to_free = it;
    it = it->next();
    delete to_free;
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
    delete to_free;
  }
}

void ChannelTrace::AddTraceEvent(Severity severity, const grpc_slice& data) {
  if (max_event_memory_ == 0) {
    grpc_slice_unref_internal(data);
    return;  // tracing is disabled if max_event_memory_ == 0
  }
  AddTraceEventHelper(new TraceEvent(severity, data));
}

void ChannelTrace::AddTraceEventWithReference(
    Severity severity, const grpc_slice& data,
    RefCountedPtr<BaseNode> referenced_entity) {
  if (max_event_memory_ == 0) {
    grpc_slice_unref_internal(data);
    return;  // tracing is disabled if max_event_memory_ == 0
  }
  // create and fill up the new event
  AddTraceEventHelper(
      new TraceEvent(severity, data, std::move(referenced_entity)));
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

Json ChannelTrace::TraceEvent::RenderTraceEvent() const {
  char* description = grpc_slice_to_c_string(data_);
  Json::Object object = {
      {"description", description},
      {"severity", severity_string(severity_)},
      {"timestamp", gpr_format_timespec(timestamp_)},
  };
  gpr_free(description);
  if (referenced_entity_ != nullptr) {
    const bool is_channel =
        (referenced_entity_->type() == BaseNode::EntityType::kTopLevelChannel ||
         referenced_entity_->type() == BaseNode::EntityType::kInternalChannel);
    object[is_channel ? "channelRef" : "subchannelRef"] = Json::Object{
        {(is_channel ? "channelId" : "subchannelId"),
         std::to_string(referenced_entity_->uuid())},
    };
  }
  return object;
}

Json ChannelTrace::RenderJson() const {
  // Tracing is disabled if max_event_memory_ == 0.
  if (max_event_memory_ == 0) {
    return Json();  // JSON null
  }
  Json::Object object = {
      {"creationTimestamp", gpr_format_timespec(time_created_)},
  };
  if (num_events_logged_ > 0) {
    object["numEventsLogged"] = std::to_string(num_events_logged_);
  }
  // Only add in the event list if it is non-empty.
  if (head_trace_ != nullptr) {
    Json::Array array;
    for (TraceEvent* it = head_trace_; it != nullptr; it = it->next()) {
      array.emplace_back(it->RenderTraceEvent());
    }
    object["events"] = std::move(array);
  }
  return object;
}

}  // namespace channelz
}  // namespace grpc_core
