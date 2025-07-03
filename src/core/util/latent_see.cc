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

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "src/core/util/backoff.h"
#include "src/core/util/notification.h"
#include "src/core/util/sync.h"

namespace grpc_core::latent_see {
std::string JsonOutput::MicrosString(int64_t nanos) {
  CHECK_GE(nanos, 0);
  const auto micros = nanos / 1000;
  const auto remainder = nanos % 1000;
  return absl::StrFormat("%d.%03d", micros, remainder);
}

void JsonOutput::Mark(absl::string_view name, int64_t tid, int64_t timestamp) {
  out_ << absl::StrCat(sep_, "{\"name\":\"", name,
                       "\",\"ph\":\"i\",\"ts\":", MicrosString(timestamp),
                       ",\"pid\":0,\"tid\":", tid, "}");
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
  out_ << absl::StrCat(sep_, "{\"name\":\"", name,
                       "\",\"ph\":\"f\",\"ts\":", MicrosString(timestamp),
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
  while (true) {
    std::unique_ptr<Bin> bin(static_cast<Bin*>(appending_.Pop()));
    if (bin == nullptr) {
      absl::SleepFor(absl::Milliseconds(backoff.NextAttemptDelay().millis()));
      continue;
    }
    backoff.Reset();
    Record(std::move(bin));
  }
}

void Sink::Start(size_t max_bins) {
  auto events = std::make_unique<EventDump>();
  MutexLock lock(&mu_);
  max_bins_ = max_bins;
  events_ = std::move(events);
}

std::unique_ptr<Sink::EventDump> Sink::Stop() {
  MutexLock lock(&mu_);
  auto events = std::move(events_);
  events_ = nullptr;
  return events;
}

void Sink::Record(std::unique_ptr<Bin> bin) {
  MutexLock lock(&mu_);
  if (events_ == nullptr) return;
  CHECK_LE(bin->num_events, Bin::kEventsPerBin);
  events_->emplace_back(std::move(bin));
  if (events_->size() > max_bins_) events_->pop_front();
}

void Collect(Notification* n, absl::Duration timeout, size_t memory_limit,
             Output* output) {
  static Sink* sink = new Sink;
  static Mutex* mu = new Mutex;

  // Collection phase - under a mutex to prevent multiple collections at once.
  mu->Lock();
  // First we enable the appender and then wait for a short time to clear out
  // any backoff
  LOG(INFO) << "Latent-see collection enabling";
  Appender::Enable(sink);
  absl::SleepFor(2 * absl::Milliseconds(kMaxBackoff.millis()));
  // Now we start the collection
  LOG(INFO) << "Latent-see collection recording";
  sink->Start(memory_limit / sizeof(Bin) + 1);
  // If we got a Notification object, use that to sleep until we're notified;
  // if not just sleep.
  if (n == nullptr) {
    absl::SleepFor(timeout);
  } else {
    n->WaitForNotificationWithTimeout(timeout);
  }
  // Grab all events
  LOG(INFO) << "Latent-see collection stopping";
  auto events = sink->Stop();
  // Disable the sink
  Appender::Disable();
  mu->Unlock();
  LOG(INFO) << "Latent-see collection stopped: processing " << events->size()
            << " bins";

  // Next: find the earliest timestamp
  // We save a lot of bytes by subtracting that out
  int64_t earliest_timestamp = std::numeric_limits<int64_t>::max();
  for (const auto& bin : *events) {
    for (const auto& event : *bin) {
      // Exclude negative timestamps as they're used for event type markers
      if (event.timestamp_begin > 0) {
        earliest_timestamp = std::min(
            {earliest_timestamp, event.timestamp_begin, event.timestamp_end});
      } else {
        earliest_timestamp = std::min(earliest_timestamp, -event.timestamp_end);
      }
    }
  }
  std::string json = "[\n";
  // TODO(ctiller): Fuschia Trace Format backend
  absl::flat_hash_map<gpr_thd_id, size_t> thread_id_map;
  for (const auto& bin : *events) {
    size_t displayed_thread_id;
    auto it = thread_id_map.find(bin->thd_id);
    if (it == thread_id_map.end()) {
      displayed_thread_id = thread_id_map.size() + 1;
      thread_id_map[bin->thd_id] = displayed_thread_id;
    } else {
      displayed_thread_id = it->second;
    }
    for (const auto& event : *bin) {
      if (event.timestamp_begin == event.timestamp_end) {
        output->Mark(event.metadata->name, displayed_thread_id,
                     event.timestamp_begin - earliest_timestamp);
      } else if (event.timestamp_begin < 0 && event.timestamp_end > 0) {
        output->FlowBegin(event.metadata->name, displayed_thread_id,
                          event.timestamp_end - earliest_timestamp,
                          -event.timestamp_begin);
      } else if (event.timestamp_begin < 0) {
        output->FlowEnd(event.metadata->name, displayed_thread_id,
                        -event.timestamp_end - earliest_timestamp,
                        -event.timestamp_begin);
      } else {
        output->Span(event.metadata->name, displayed_thread_id,
                     event.timestamp_begin - earliest_timestamp,
                     event.timestamp_end - event.timestamp_begin);
      }
    }
  }
  output->Finish();
  LOG(INFO) << "Latent-see collection complete";
}

}  // namespace latent_see
}  // namespace grpc_core
#endif
