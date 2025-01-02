//
//
// Copyright 2017 gRPC authors.
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
//
//

#include "src/core/channelz/channel_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>

#include "absl/strings/str_cat.h"
#include "src/core/channelz/channelz.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/string.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace channelz {

//
// ChannelTrace::TraceEvent
//

ChannelTrace::TraceEvent::TraceEvent(Severity severity, const grpc_slice& data,
                                     RefCountedPtr<BaseNode> referenced_entity)
    : timestamp_(Timestamp::Now().as_timespec(GPR_CLOCK_REALTIME)),
      severity_(severity),
      data_(data),
      memory_usage_(sizeof(TraceEvent) + grpc_slice_memory_usage(data)),
      referenced_entity_(std::move(referenced_entity)) {}

ChannelTrace::TraceEvent::TraceEvent(Severity severity, const grpc_slice& data)
    : TraceEvent(severity, data, nullptr) {}

ChannelTrace::TraceEvent::~TraceEvent() { CSliceUnref(data_); }

namespace {

const char* SeverityString(ChannelTrace::Severity severity) {
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
      {"description", Json::FromString(description)},
      {"severity", Json::FromString(SeverityString(severity_))},
      {"timestamp", Json::FromString(gpr_format_timespec(timestamp_))},
  };
  gpr_free(description);
  if (referenced_entity_ != nullptr) {
    const bool is_channel =
        (referenced_entity_->type() == BaseNode::EntityType::kTopLevelChannel ||
         referenced_entity_->type() == BaseNode::EntityType::kInternalChannel);
    object[is_channel ? "channelRef" : "subchannelRef"] = Json::FromObject({
        {(is_channel ? "channelId" : "subchannelId"),
         Json::FromString(absl::StrCat(referenced_entity_->uuid()))},
    });
  }
  return Json::FromObject(std::move(object));
}

//
// ChannelTrace
//

ChannelTrace::ChannelTrace(size_t max_event_memory)
    : max_event_memory_(max_event_memory),
      time_created_(Timestamp::Now().as_timespec(GPR_CLOCK_REALTIME)) {}

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
}

void ChannelTrace::AddTraceEventHelper(TraceEvent* new_trace_event) {
  MutexLock lock(&mu_);
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
    CSliceUnref(data);
    return;  // tracing is disabled if max_event_memory_ == 0
  }
  AddTraceEventHelper(new TraceEvent(severity, data));
}

void ChannelTrace::AddTraceEventWithReference(
    Severity severity, const grpc_slice& data,
    RefCountedPtr<BaseNode> referenced_entity) {
  if (max_event_memory_ == 0) {
    CSliceUnref(data);
    return;  // tracing is disabled if max_event_memory_ == 0
  }
  // create and fill up the new event
  AddTraceEventHelper(
      new TraceEvent(severity, data, std::move(referenced_entity)));
}

Json ChannelTrace::RenderJson() const {
  // Tracing is disabled if max_event_memory_ == 0.
  if (max_event_memory_ == 0) {
    return Json();  // JSON null
  }
  Json::Object object = {
      {"creationTimestamp",
       Json::FromString(gpr_format_timespec(time_created_))},
  };
  MutexLock lock(&mu_);
  if (num_events_logged_ > 0) {
    object["numEventsLogged"] =
        Json::FromString(absl::StrCat(num_events_logged_));
  }
  // Only add in the event list if it is non-empty.
  if (head_trace_ != nullptr) {
    Json::Array array;
    for (TraceEvent* it = head_trace_; it != nullptr; it = it->next()) {
      array.emplace_back(it->RenderTraceEvent());
    }
    object["events"] = Json::FromArray(std::move(array));
  }
  return Json::FromObject(std::move(object));
}

}  // namespace channelz
}  // namespace grpc_core
