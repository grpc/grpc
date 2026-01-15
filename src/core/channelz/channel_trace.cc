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
#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>
#include <utility>

#include "google/protobuf/any.upb.h"
#include "src/core/channelz/channelz.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/util/upb_utils.h"
#include "src/proto/grpc/channelz/v2/channelz.upb.h"
#include "absl/strings/str_cat.h"
#include "upb/base/string_view.h"

namespace grpc_core {
namespace channelz {

//
// ChannelTrace
//

std::string ChannelTrace::creation_timestamp() const {
  return gpr_format_timespec(time_created_.as_timespec(GPR_CLOCK_REALTIME));
}

ChannelTrace::EntryRef ChannelTrace::AppendEntry(
    EntryRef parent, std::unique_ptr<Renderer> renderer) {
  if (max_memory_ == 0) return EntryRef::Sentinel();
  MutexLock lock(&mu_);
  ++num_events_logged_;
  const auto ref = NewEntry(parent, std::move(renderer));
  while (current_memory_ > max_memory_ && first_entry_ != kSentinelId) {
    DropEntryId(first_entry_);
  }
  if (GPR_UNLIKELY(current_memory_ > max_memory_)) {
    entries_.shrink_to_fit();
    current_memory_ = MemoryUsageOf(entries_);
  }
  return ref;
}

ChannelTrace::EntryRef ChannelTrace::NewEntry(
    EntryRef parent, std::unique_ptr<Renderer> renderer) {
  if (parent.id != kSentinelId) {
    if (parent.id >= entries_.size()) return EntryRef::Sentinel();
    if (parent.salt != entries_[parent.id].salt) {
      // Parent no longer present: no point adding child
      return EntryRef::Sentinel();
    }
  }
  uint16_t id;
  if (next_free_entry_ != kSentinelId) {
    id = next_free_entry_;
    next_free_entry_ = entries_[id].next_chronologically;
  } else {
    id = entries_.size();
    DCHECK_NE(id, kSentinelId);
    entries_.emplace_back();
    current_memory_ = MemoryUsageOf(entries_);
  }
  Entry& e = entries_[id];
  e.when = Timestamp::Now();
  e.parent = parent.id;
  e.first_child = kSentinelId;
  e.last_child = kSentinelId;
  e.prev_sibling = kSentinelId;
  e.next_sibling = kSentinelId;
  e.next_chronologically = kSentinelId;
  e.renderer = std::move(renderer);
  e.prev_chronologically = last_entry_;
  if (last_entry_ == kSentinelId) {
    DCHECK_EQ(first_entry_, kSentinelId);
    first_entry_ = id;
  } else {
    Entry& last = entries_[last_entry_];
    DCHECK_EQ(last.next_chronologically, kSentinelId);
    last.next_chronologically = id;
  }
  last_entry_ = id;
  if (parent.id != kSentinelId) {
    Entry& p = entries_[parent.id];
    e.prev_sibling = p.last_child;
    if (p.last_child == kSentinelId) {
      DCHECK_EQ(p.first_child, kSentinelId);
      p.first_child = id;
    } else {
      Entry& last = entries_[p.last_child];
      DCHECK_EQ(last.next_sibling, kSentinelId);
      last.next_sibling = id;
    }
    p.last_child = id;
  }
  current_memory_ += e.renderer->MemoryUsage();
  DCHECK_EQ(MemoryUsageOf(entries_), current_memory_);
  return EntryRef{id, e.salt};
}

void ChannelTrace::DropEntry(EntryRef entry) {
  if (entry.id == kSentinelId) return;
  MutexLock lock(&mu_);
  if (entry.id >= entries_.size()) return;
  Entry& e = entries_[entry.id];
  if (e.salt != entry.salt) return;
  DropEntryId(entry.id);
}

void ChannelTrace::DropEntryId(uint16_t id) {
  Entry& e = entries_[id];
  while (e.first_child != kSentinelId) {
    DropEntryId(e.first_child);
  }
  if (e.prev_chronologically != kSentinelId) {
    Entry& prev = entries_[e.prev_chronologically];
    DCHECK_EQ(prev.next_chronologically, id);
    prev.next_chronologically = e.next_chronologically;
  }
  if (e.next_chronologically != kSentinelId) {
    Entry& next = entries_[e.next_chronologically];
    DCHECK_EQ(next.prev_chronologically, id);
    next.prev_chronologically = e.prev_chronologically;
  }
  if (e.prev_sibling != kSentinelId) {
    Entry& prev = entries_[e.prev_sibling];
    DCHECK_EQ(prev.next_sibling, id);
    prev.next_sibling = e.next_sibling;
  }
  if (e.next_sibling != kSentinelId) {
    Entry& next = entries_[e.next_sibling];
    DCHECK_EQ(next.prev_sibling, id);
    next.prev_sibling = e.prev_sibling;
  }
  if (e.parent != kSentinelId) {
    Entry& p = entries_[e.parent];
    if (p.first_child == id) {
      p.first_child = e.next_sibling;
    }
    if (p.last_child == id) {
      p.last_child = e.prev_sibling;
    }
  }
  if (first_entry_ == id) {
    first_entry_ = e.next_chronologically;
  }
  if (last_entry_ == id) {
    last_entry_ = e.prev_chronologically;
  }
  ++e.salt;
  e.next_chronologically = next_free_entry_;
  current_memory_ -= e.renderer->MemoryUsage();
  e.renderer.reset();
  DCHECK_EQ(current_memory_, MemoryUsageOf(entries_));
  next_free_entry_ = id;
}

void ChannelTrace::ForEachTraceEvent(
    absl::FunctionRef<void(gpr_timespec, std::string)> callback) const {
  MutexLock lock(&mu_);
  ForEachTraceEventLocked(callback);
}

void ChannelTrace::ForEachTraceEventLocked(
    absl::FunctionRef<void(gpr_timespec, std::string)> callback) const {
  uint16_t id = first_entry_;
  while (id != kSentinelId) {
    const Entry& e = entries_[id];
    if (e.parent == kSentinelId) RenderEntry(e, callback, 0);
    id = e.next_chronologically;
  }
}

void ChannelTrace::RenderEntry(
    const Entry& entry,
    absl::FunctionRef<void(gpr_timespec, std::string)> callback,
    int depth) const {
  if (entry.renderer != nullptr) {
    callback(entry.when.as_timespec(GPR_CLOCK_REALTIME),
             entry.renderer->Render());
  } else if (entry.first_child != kSentinelId) {
    callback(entry.when.as_timespec(GPR_CLOCK_REALTIME),
             "?unknown parent entry?");
  }
  if (entry.first_child != kSentinelId) {
    uint16_t id = entry.first_child;
    while (id != kSentinelId) {
      const Entry& e = entries_[id];
      RenderEntry(e, callback, depth + 1);
      id = e.next_sibling;
    }
  }
}

void ChannelTrace::Render(grpc_channelz_v2_Entity* entity,
                          upb_Arena* arena) const {
  MutexLock lock(&mu_);
  uint16_t id = first_entry_;
  while (id != kSentinelId) {
    const Entry& e = entries_[id];
    if (e.parent == kSentinelId) {
      RenderEntry(e, grpc_channelz_v2_Entity_add_trace(entity, arena), arena);
    }
    id = e.next_chronologically;
  }
}

void ChannelTrace::RenderEntry(const Entry& entry,
                               grpc_channelz_v2_TraceEvent* trace_event,
                               upb_Arena* arena) const {
  TimestampToUpb(
      entry.when.as_timespec(GPR_CLOCK_REALTIME),
      grpc_channelz_v2_TraceEvent_mutable_timestamp(trace_event, arena));
  if (entry.renderer != nullptr) {
    grpc_channelz_v2_TraceEvent_set_description(
        trace_event, CopyStdStringToUpbString(entry.renderer->Render(), arena));
  }
  for (uint16_t id = entry.first_child; id != kSentinelId;
       id = entries_[id].next_sibling) {
    auto* child = grpc_channelz_v2_TraceEvent_new(arena);
    RenderEntry(entries_[id], child, arena);
    size_t length;
    auto* bytes = grpc_channelz_v2_TraceEvent_serialize(child, arena, &length);
    auto* data = grpc_channelz_v2_Data_new(arena);
    grpc_channelz_v2_Data_set_name(data, StdStringToUpbString("child_trace"));
    auto* any = grpc_channelz_v2_Data_mutable_value(data, arena);
    google_protobuf_Any_set_value(
        any, upb_StringView_FromDataAndSize(bytes, length));
    google_protobuf_Any_set_type_url(
        any, StdStringToUpbString(
                 "type.googleapis.com/grpc.channelz.v2.TraceEvent"));
  }
}

size_t testing::GetSizeofTraceEvent() { return sizeof(ChannelTrace::Entry); }

}  // namespace channelz
}  // namespace grpc_core
