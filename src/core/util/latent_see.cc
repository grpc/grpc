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
namespace grpc_core {
namespace latent_see {

std::string Log::GenerateJson() {
  auto& log = Get();
  std::vector<RecordedEvent> events;
  for (auto& fragment : log.fragments_) {
    MutexLock lock(&fragment.mu);
    events.insert(events.end(), fragment.events.begin(), fragment.events.end());
  }
  std::stable_sort(events.begin(), events.end(),
                   [](const RecordedEvent& a, const RecordedEvent& b) {
                     return a.batch_id < b.batch_id;
                   });
  std::string json = "[";
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
    if (!first) absl::StrAppend(&json, ",");
    first = false;
    absl::StrAppend(&json, "{\"name\": ", event.event.metadata->name,
                    "\"ph\": \"", phase, "\", \"ts\": ", event.event.timestamp,
                    ", \"pid\": 0, \"tid\": ", event.thread_id,
                    ", \"args\": {\"file\": \"", event.event.metadata->file,
                    "\", \"line\": ", event.event.metadata->line, "}}");
  }
  absl::StrAppend(&json, "]");
  return json;
}

}  // namespace latent_see
}  // namespace grpc_core
#endif
