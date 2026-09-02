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

#include <grpc/support/port_platform.h>

#include "src/core/util/latent_see.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/channelz/property_list.h"
#include "src/core/util/backoff.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/notification.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc_core::latent_see {

thread_local std::unique_ptr<Bin> Appender::bin_;
std::atomic<Sink*> Appender::active_sink_;

std::string JsonOutput::MicrosString(int64_t nanos) {
  GRPC_CHECK_GE(nanos, 0);
  const int64_t micros = nanos / 1000;
  const int64_t remainder = nanos % 1000;
  return absl::StrFormat("%d.%03d", micros, remainder);
}

void JsonOutput::Mark(absl::string_view name, int64_t tid, int64_t timestamp,
                      channelz::PropertyList property_list) {
  Json properties_json = Json::FromObject(property_list.TakeJsonObject());
  out_ << absl::StrCat(sep_, "{\"name\":\"", name,
                       "\",\"ph\":\"i\",\"ts\":", MicrosString(timestamp),
                       ",\"pid\":0,\"tid\":", tid,
                       ",\"args\":", JsonDump(properties_json), "}");
  sep_ = ",\n";
}

void JsonOutput::FlowBegin(absl::string_view name, int64_t tid,
                           int64_t timestamp, int64_t flow_id) {
  out_ << absl::StrCat(sep_, "{\"name\":\"", name,
                       "\",\"ph\":\"s\",\"ts\":", MicrosString(timestamp),
                       ",\"pid\":0,\"tid\":", tid, ",\"id\":", flow_id, "}");
  sep_ = ",\n";
}

void JsonOutput::FlowEnd(absl::string_view name, int64_t tid, int64_t timestamp,
                         int64_t flow_id) {
  out_ << absl::StrCat(
      sep_, "{\"name\":\"", name,
      "\",\"ph\":\"f\",\"bp\":\"e\",\"ts\":", MicrosString(timestamp),
      ",\"pid\":0,\"tid\":", tid, ",\"id\":", flow_id, "}");
  sep_ = ",\n";
}

void JsonOutput::Span(absl::string_view name, int64_t tid,
                      int64_t timestamp_begin, int64_t duration) {
  out_ << absl::StrCat(sep_, "{\"name\":\"", name,
                       "\",\"ph\":\"X\",\"ts\":", MicrosString(timestamp_begin),
                       ",\"pid\":0,\"tid\":", tid,
                       ",\"dur\":", MicrosString(duration), "}");
  sep_ = ",\n";
}

void JsonOutput::Finish() { out_ << "\n]"; }
}  // namespace grpc_core::latent_see

#ifndef GRPC_DISABLE_LATENT_SEE

namespace grpc_core {
namespace latent_see {

namespace {
const Duration kMaxBackoff = Duration::Milliseconds(300);
}

void Appender::Enable(Sink* sink) {
  active_sink_.store(sink, std::memory_order_release);
}

void Appender::Disable() {
  active_sink_.store(nullptr, std::memory_order_relaxed);
}

Sink::Sink() : gatherer_("grpc_latent_see_gatherer", [this]() { Gather(); }) {
  gatherer_.Start();
}

void Sink::Append(std::unique_ptr<Bin> bin) { appending_.Push(bin.release()); }

void Sink::Gather() {
  BackOff backoff(BackOff::Options()
                      .set_initial_backoff(Duration::Milliseconds(1))
                      .set_multiplier(1.1)
                      .set_jitter(0.05)
                      .set_max_backoff(kMaxBackoff));
  // Pre-allocated batch buffer to avoid heap allocations in the gatherer loop.
  std::vector<std::unique_ptr<Bin>> batch;
  batch.reserve(64u);
  while (true) {
    // Wait until collection is actively started or draining is requested.
    {
      MutexLock lock(mu_);
      while (events_ == nullptr && !draining_) {
        cv_.Wait(&mu_);
      }
    }

    while (true) {
      batch.clear();
      bool empty = false;
      // Pop up to 64 ready bins from the lock-free queue without holding mu_.
      while (batch.size() < 64u) {
        std::unique_ptr<Bin> bin(
            static_cast<Bin*>(appending_.PopAndCheckEnd(&empty)));
        if (bin == nullptr) break;
        batch.emplace_back(std::move(bin));
      }

      if (!batch.empty()) {
        backoff.Reset();
        MutexLock lock(mu_);
        if (GPR_LIKELY(events_ != nullptr)) {
          for (std::unique_ptr<Bin>& bin : batch) {
            RecordLocked(std::move(bin));
          }
        }
        continue;
      }

      bool is_draining = false;
      {
        MutexLock lock(mu_);
        is_draining = draining_;
      }

      if (is_draining && empty) {
        // Complete draining and notify the stopping thread.
        MutexLock lock(mu_);
        draining_ = false;
        cv_.Signal();
        break;
      }

      if (!empty) {
        // In-flight push in progress from a producer; yield briefly.
        absl::SleepFor(absl::Microseconds(10));
        continue;
      }

      // Queue is empty during active recording; wait with backoff timeout.
      {
        MutexLock lock(mu_);
        if (events_ != nullptr && !draining_) {
          cv_.WaitWithTimeout(&mu_,
                              absl::Milliseconds(backoff.NextAttemptDelay().millis()));
        } else {
          break;
        }
      }
    }
  }
}

void Sink::Start(size_t max_bins) {
  std::unique_ptr<EventDump> events = std::make_unique<EventDump>();
  MutexLock lock(mu_);
  max_bins_ = max_bins;
  events_ = std::move(events);
  cv_.Signal();
}

std::unique_ptr<Sink::EventDump> Sink::Stop() {
  MutexLock lock(mu_);
  draining_ = true;
  cv_.Signal();
  while (draining_) {
    cv_.Wait(&mu_);
  }
  std::unique_ptr<EventDump> events = std::move(events_);
  events_ = nullptr;
  return events;
}

void Sink::RecordLocked(std::unique_ptr<Bin> bin) {
  if (GPR_UNLIKELY(events_ == nullptr)) return;
  events_->emplace_back(std::move(bin));
  if (GPR_UNLIKELY(events_->size() > max_bins_)) events_->pop_front();
}

void Collect(Notification* n, absl::Duration timeout, size_t memory_limit,
             Output* output) {
  static Sink* sink = new Sink;
  static Mutex* mu = new Mutex;

  // Collection phase - under a mutex to prevent multiple collections at once.
  MutexLock lock(*mu);
  LOG(INFO) << "Latent-see collection starting";
  // Start the sink before enabling appender to ensure events are not dropped.
  sink->Start(memory_limit / sizeof(Bin) + 1u);
  Appender::Enable(sink);
  LOG(INFO) << "Latent-see collection recording";
  // If we got a Notification object, use that to sleep until we're notified;
  // if not just sleep.
  if (n == nullptr) {
    absl::SleepFor(timeout);
  } else {
    n->WaitForNotificationWithTimeout(timeout);
  }
  // Grab all events
  LOG(INFO) << "Latent-see collection stopping";
  // Disable appender first to prevent new incoming events while draining.
  Appender::Disable();
  std::unique_ptr<Sink::EventDump> events = sink->Stop();
  GRPC_CHECK(events != nullptr);
  LOG(INFO) << "Latent-see collection stopped: processing " << events->size()
            << " bins";

  // Next: find the earliest timestamp
  // We save a lot of bytes by subtracting that out
  int64_t earliest_timestamp = std::numeric_limits<int64_t>::max();
  for (const auto& bin : *events) {
    for (const auto& event : *bin) {
      // Exclude negative timestamps as they're used for event type markers.
      if (event.timestamp_begin > 0) {
        earliest_timestamp = std::min(
            {earliest_timestamp, event.timestamp_begin, event.timestamp_end});
      } else {
        earliest_timestamp =
            std::min(earliest_timestamp, std::abs(event.timestamp_end));
      }
    }
  }
  std::string json = "[\n";
  // Add a mark for the actual timestamp when collection started.
  output->Mark(
      "LatentseeCollectionStart", /*tid=*/0, /*timestamp=*/0,
      channelz::PropertyList().Set("actual_start_time", earliest_timestamp));
  // TODO(ctiller): Fuschia Trace Format backend
  absl::flat_hash_map<gpr_thd_id, size_t> thread_id_map;
  for (const auto& bin : *events) {
    size_t displayed_thread_id;
    auto it = thread_id_map.find(bin->thd_id);
    if (it == thread_id_map.end()) {
      displayed_thread_id = thread_id_map.size() + 1u;
      thread_id_map[bin->thd_id] = displayed_thread_id;
    } else {
      displayed_thread_id = it->second;
    }
    for (auto it = bin->begin(); it != bin->end(); ++it) {
      if (it->timestamp_begin == it->timestamp_end) {
        if (it->metadata->extra_event_size > 0) {
          output->Mark(
              it->metadata->name, displayed_thread_id,
              it->timestamp_begin - earliest_timestamp,
              it->metadata->to_property_list(it.ptr() + sizeof(Bin::Event)));
        } else {
          output->Mark(it->metadata->name, displayed_thread_id,
                       it->timestamp_begin - earliest_timestamp,
                       channelz::PropertyList());
        }
      } else if (it->timestamp_begin < 0 && it->timestamp_end > 0) {
        output->FlowBegin(it->metadata->name, displayed_thread_id,
                          it->timestamp_end - earliest_timestamp,
                          -it->timestamp_begin);
      } else if (it->timestamp_begin < 0) {
        output->FlowEnd(it->metadata->name, displayed_thread_id,
                        -it->timestamp_end - earliest_timestamp,
                        -it->timestamp_begin);
      } else {
        output->Span(it->metadata->name, displayed_thread_id,
                     it->timestamp_begin - earliest_timestamp,
                     it->timestamp_end - it->timestamp_begin);
      }
    }
  }
  output->Finish();
  LOG(INFO) << "Latent-see collection complete";
}

}  // namespace latent_see
}  // namespace grpc_core
#endif
