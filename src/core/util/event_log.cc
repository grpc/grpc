// Copyright 2022 gRPC authors.
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

#include "src/core/util/event_log.h"

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <atomic>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

std::atomic<EventLog*> EventLog::g_instance_{nullptr};

EventLog::~EventLog() {
  CHECK(g_instance_.load(std::memory_order_acquire) != this);
}

void EventLog::BeginCollection() {
  for (auto& fragment : fragments_) {
    MutexLock lock(&fragment.mu);
    fragment.entries.clear();
  }
  collection_begin_ = gpr_get_cycle_counter();
  g_instance_.store(this, std::memory_order_release);
  Append("logging", 1);
}

std::vector<EventLog::Entry> EventLog::EndCollection(
    absl::Span<const absl::string_view> wanted_events) {
  Append("logging", -1);
  g_instance_.store(nullptr, std::memory_order_release);
  std::vector<Entry> result;
  for (auto& fragment : fragments_) {
    MutexLock lock(&fragment.mu);
    for (const auto& entry : fragment.entries) {
      if (std::find(wanted_events.begin(), wanted_events.end(), entry.event) !=
          wanted_events.end()) {
        result.push_back(entry);
      }
    }
    fragment.entries.clear();
  }
  std::stable_sort(
      result.begin(), result.end(),
      [](const Entry& a, const Entry& b) { return a.when < b.when; });
  return result;
}

void EventLog::AppendInternal(absl::string_view event, int64_t delta) {
  auto& fragment = fragments_.this_cpu();
  MutexLock lock(&fragment.mu);
  fragment.entries.push_back({gpr_get_cycle_counter(), event, delta});
}

std::string EventLog::EndCollectionAndReportCsv(
    absl::Span<const absl::string_view> columns) {
  auto events = EndCollection(columns);
  std::vector<int64_t> values(columns.size(), 0);
  std::string result =
      absl::StrCat("timestamp,", absl::StrJoin(columns, ","), "\n");
  for (const auto& entry : events) {
    auto idx = std::find(columns.begin(), columns.end(), entry.event) -
               columns.begin();
    values[idx] += entry.delta;
    absl::StrAppend(&result, entry.when - collection_begin_, ",",
                    absl::StrJoin(values, ","), "\n");
  }
  return result;
}

}  // namespace grpc_core
