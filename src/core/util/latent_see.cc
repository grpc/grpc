// Copyright 2024 gRPC authors.
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

#include "src/core/util/latent_see.h"

#ifdef GRPC_ENABLE_LATENT_SEE
#include <chrono>
#include <cstdint>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

namespace grpc_core {
namespace latent_see {

thread_local std::vector<Log::Event> Log::thread_events_;
thread_local uint64_t Log::thread_id_ = Log::Get().next_thread_id_.fetch_add(1);
std::atomic<uint64_t> Flow::next_flow_id_{1};

std::string Log::GenerateJson() {
  std::vector<RecordedEvent> events;
  for (auto& fragment : fragments_) {
    MutexLock lock(&fragment.mu);
    events.insert(events.end(), fragment.events.begin(), fragment.events.end());
  }
  absl::optional<std::chrono::steady_clock::time_point> start_time;
  for (auto& event : events) {
    if (!start_time.has_value() || *start_time > event.event.timestamp) {
      start_time = event.event.timestamp;
    }
  }
  if (!start_time.has_value()) return "[]";
  std::string json = "[\n";
  bool first = true;
  for (const auto& event : events) {
    absl::string_view phase;
    bool has_id;
    switch (event.event.type) {
      case EventType::kBegin:
        phase = "B";
        has_id = false;
        break;
      case EventType::kEnd:
        phase = "E";
        has_id = false;
        break;
      case EventType::kFlowStart:
        phase = "s";
        has_id = true;
        break;
      case EventType::kFlowEnd:
        phase = "f";
        has_id = true;
        break;
      case EventType::kMark:
        phase = "i";
        has_id = false;
        break;
    }
    if (!first) absl::StrAppend(&json, ",\n");
    first = false;
    absl::StrAppend(&json, "{\"name\": ", event.event.metadata->name,
                    ", \"ph\": \"", phase, "\", \"ts\": ",
                    std::chrono::duration<double, std::micro>(
                        event.event.timestamp - *start_time)
                        .count(),
                    ", \"pid\": 0, \"tid\": ", event.thread_id);
    if (has_id) {
      absl::StrAppend(&json, ", \"id\": ", event.event.id);
    }
    if (event.event.type == EventType::kFlowEnd) {
      absl::StrAppend(&json, ", \"bp\": \"e\"");
    }
    absl::StrAppend(&json, ", \"args\": {\"file\": \"",
                    event.event.metadata->file,
                    "\", \"line\": ", event.event.metadata->line,
                    ", \"batch\": ", event.batch_id, "}}");
  }
  absl::StrAppend(&json, "\n]");
  return json;
}

void Log::FlushThreadLog() {
  auto& thread_events = thread_events_;
  if (thread_events.empty()) return;
  auto& log = Get();
  const auto batch_id =
      log.next_batch_id_.fetch_add(1, std::memory_order_relaxed);
  auto& fragment = log.fragments_.this_cpu();
  const auto thread_id = thread_id_;
  {
    MutexLock lock(&fragment.mu);
    for (auto event : thread_events) {
      fragment.events.push_back(RecordedEvent{thread_id, batch_id, event});
    }
  }
  thread_events.clear();
}

}  // namespace latent_see
}  // namespace grpc_core
#endif
