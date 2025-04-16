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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/util/ring_buffer.h"
#include "src/core/util/sync.h"

namespace grpc_core {
namespace latent_see {

thread_local uint64_t Log::thread_id_ = Log::Get().next_thread_id_.fetch_add(1);
thread_local Bin* Log::bin_ = nullptr;
thread_local void* Log::bin_owner_ = nullptr;
std::atomic<uint64_t> Flow::next_flow_id_{1};
std::atomic<uintptr_t> Log::free_bins_{0};
const std::chrono::steady_clock::time_point start_time =
    std::chrono::steady_clock::now();

void Log::TryPullEventsAndFlush(
    absl::FunctionRef<void(absl::Span<const RecordedEvent>)> callback) {
  // Try to lock... if we fail then clear the active events.
  // This guarantees freeing up memory even if we're still serializing the
  // previous pull.
  if (!mu_flushing_.TryLock()) {
    for (auto& fragment : fragments_) {
      MutexLock lock(&fragment.mu_active);
      fragment.active.clear();
    }
    return;
  }
  // Now we hold the lock; swap all active fragments to flushing.
  // This is relatively quick and ensures that we don't stall capture for
  // long.
  for (auto& fragment : fragments_) {
    CHECK_EQ(fragment.flushing.size(), 0);
    MutexLock lock(&fragment.mu_active);
    fragment.flushing.swap(fragment.active);
  }
  // Now we've swapped out, call the callback repeatedly with each fragment.
  // This is the slow part - there's a lot of copying and transformation that
  // happens here.
  // We keep the mu_flushing_ held so that we can guarantee only one thread is
  // consumed by this at a time.
  // Once we've called the callback for each fragment we can clear it, so that
  // when we next swap it with the active fragment it will be empty.
  // This also retains the memory, so if we're serializing with a regular
  // cadence we'll tend to stabilize memory usage for latent_see relatively
  // quickly.
  for (auto& fragment : fragments_) {
    callback(fragment.flushing);
    fragment.flushing.clear();
  }
  mu_flushing_.Unlock();
}

std::optional<std::string> Log::TryGenerateJson() {
  using Nanos = std::chrono::duration<unsigned long long, std::nano>;
  std::string json = "[\n";
  bool first = true;
  int callbacks = 0;
  TryPullEventsAndFlush([&](absl::Span<const RecordedEvent> events) {
    ++callbacks;
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
      if (!first) {
        absl::StrAppend(&json, ",\n");
      }
      first = false;
      if (event.event.metadata->name[0] != '"') {
        absl::StrAppend(
            &json, "{\"name\": \"", event.event.metadata->name,
            "\", \"ph\": \"", phase, "\", \"ts\": ",
            Nanos(event.event.timestamp - start_time).count() / 1000.0,
            ", \"pid\": 0, \"tid\": ", event.thread_id);
      } else {
        absl::StrAppend(
            &json, "{\"name\": ", event.event.metadata->name, ", \"ph\": \"",
            phase, "\", \"ts\": ",
            Nanos(event.event.timestamp - start_time).count() / 1000.0,
            ", \"pid\": 0, \"tid\": ", event.thread_id);
      }

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
  });
  if (callbacks == 0) return std::nullopt;
  absl::StrAppend(&json, "\n]");
  return json;
}

void Log::FlushBin(Bin* bin) {
  if (bin->events.empty()) return;
  auto& log = Get();
  const auto batch_id =
      log.next_batch_id_.fetch_add(1, std::memory_order_relaxed);
  auto& fragment = log.fragments_.this_cpu();
  const auto thread_id = thread_id_;
  {
    MutexLock lock(&fragment.mu_active);
    for (auto event : bin->events) {
      fragment.active.push_back(RecordedEvent{thread_id, batch_id, event});
    }
  }
  bin->events.clear();
}

}  // namespace latent_see
}  // namespace grpc_core
#endif
