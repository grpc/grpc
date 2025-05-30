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
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace channelz {

//
// ChannelTrace
//

Json ChannelTrace::RenderJson() const {
  Json::Array array;
  ForEachTraceEvent([&array](gpr_timespec timestamp, Severity, std::string line,
                             RefCountedPtr<BaseNode>) {
    Json::Object object = {
        {"severity", Json::FromString("CT_INFO")},
        {"timestamp", Json::FromString(gpr_format_timespec(timestamp))},
        {"description", Json::FromString(std::move(line))},
    };
    array.push_back(Json::FromObject(std::move(object)));
  });
  Json::Object object;
  if (!array.empty()) {
    object["events"] = Json::FromArray(std::move(array));
  }
  return Json::FromObject(std::move(object));
}

ChannelTrace::EntryRef ChannelTrace::AppendEntry(
    EntryRef parent, std::unique_ptr<Renderer> renderer) {
  if (max_memory_ == 0) return EntryRef::Sentinel();
  MutexLock lock(&mu_);
  const auto ref = NewEntry(parent, std::move(renderer));
  while (current_memory_ > max_memory_ && first_entry_ != kSentinelId) {
    DropEntryId(first_entry_);
  }
  if (GPR_UNLIKELY(current_memory_ > max_memory_)) {
    entries_.shrink_to_fit();
    current_memory_ = MemoryUsage(entries_);
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
    current_memory_ = MemoryUsage(entries_);
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
  DCHECK_EQ(MemoryUsage(entries_), current_memory_);
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
  DCHECK_EQ(current_memory_, MemoryUsage(entries_));
  next_free_entry_ = id;
}

void ChannelTrace::ForEachTraceEvent(
    absl::FunctionRef<void(gpr_timespec, Severity, std::string,
                           RefCountedPtr<BaseNode>)>
        callback) const {
  MutexLock lock(&mu_);
  uint16_t id = first_entry_;
  while (id != kSentinelId) {
    const Entry& e = entries_[id];
    if (e.parent == kSentinelId) RenderEntry(e, callback, 0);
    id = e.next_chronologically;
  }
}

void ChannelTrace::RenderEntry(
    const Entry& entry,
    absl::FunctionRef<void(gpr_timespec, Severity, std::string,
                           RefCountedPtr<BaseNode>)>
        callback,
    int depth) const {
  if (entry.renderer != nullptr) {
    callback(entry.when.as_timespec(GPR_CLOCK_REALTIME), Severity::Info,
             entry.renderer->Render(), nullptr);
  } else if (entry.first_child != kSentinelId) {
    callback(entry.when.as_timespec(GPR_CLOCK_REALTIME), Severity::Info,
             "?unknown parent entry?", nullptr);
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

size_t testing::GetSizeofTraceEvent() { return sizeof(ChannelTrace::Entry); }

}  // namespace channelz
}  // namespace grpc_core
