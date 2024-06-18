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

#ifndef LATENT_SEE_H
#define LATENT_SEE_H

#include <cstdint>
#include <vector>

#include "time_precise.h"

#include "src/core/lib/gprpp/per_cpu.h"
#include "src/core/lib/gprpp/sync.h"

#define GRPC_ENABLE_LATENT_SEE

#ifdef GRPC_ENABLE_LATENT_SEE
namespace grpc_core {
namespace latent_see {

struct Metadata {
  const char* file;
  int line;
  const char* name;
};

enum class EventType : uint8_t { kBegin, kEnd };

class Log {
 public:
  static void FlushThreadLog() {
    auto& log = Get();
    auto batch_id = log.next_batch_id_.fetch_add(1, std::memory_order_relaxed);
    auto& fragment = log.fragments_.this_cpu();
    MutexLock lock(&fragment.mu);
    fragment.events.push_back(
        Batch{thread_id_, batch_id, std::move(thread_events_)});
    thread_events_.clear();
  }

  static void Append(const Metadata* metadata, EventType type) {
    thread_events_.push_back(Event{metadata, gpr_get_cycle_counter(), type});
  }

 private:
  Log() = default;

  static Log& Get() {
    static Log* log = new Log();
    return *log;
  }

  struct Event {
    const Metadata* metadata;
    gpr_cycle_counter timestamp;
    EventType type;
  };
  struct Batch {
    uint64_t thread_id;
    uint64_t batch_id;
    std::vector<Event> events;
  };
  std::atomic<uint64_t> next_thread_id_{0};
  std::atomic<uint64_t> next_batch_id_{0};
  static thread_local std::vector<Event> thread_events_;
  static thread_local uint64_t thread_id_;
  struct Fragment {
    Mutex mu;
    std::vector<Batch> events ABSL_GUARDED_BY(mu);
  };
  PerCpu<Fragment> fragments_{PerCpuOptions()};
};

class Scope {
 public:
  explicit Scope(const Metadata* metadata) : metadata_(metadata) {
    Log::Append(metadata_, EventType::kBegin);
  }
  ~Scope() { Log::Append(metadata_, EventType::kEnd); }

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  const Metadata* const metadata_;
};

class ParentScope : public Scope {
 public:
  using Scope::Scope;
  ~ParentScope() { Log::FlushThreadLog(); }
};
}  // namespace latent_see
}  // namespace grpc_core
#define GRPC_LATENT_SEE_CAPTURE_METADATA(name)                     \
  static const grpc_core::latent_see::Metadata name##_metadata = { \
      __FILE__, __LINE__, #name}
#define GRPC_LATENT_SEE_METADATA(name) &name##_metadata
#define GRPC_LATENT_SEE_PARENT_SCOPE(name) \
  GRPC_LATENT_SEE_CAPTURE_METADATA(name)   \
  grpc_core::latent_see::ParentScope name(GRPC_LATENT_SEE_METADATA(name))
#define GRPC_LATENT_SEE_SCOPE(name)      \
  GRPC_LATENT_SEE_CAPTURE_METADATA(name) \
  grpc_core::latent_see::Scope name(GRPC_LATENT_SEE_METADATA(name))
#define GRPC_LATENT_SEE_MARK(name)       \
  GRPC_LATENT_SEE_CAPTURE_METADATA(name) \
  grpc_core::latent_see::Mark(GRPC_LATENT_SEE_METADATA(name))
#define GRPC_LATENT_SEE_FLOW(name)       \
  GRPC_LATENT_SEE_CAPTURE_METADATA(name) \
  grpc_core::latent_see::Flow(GRPC_LATENT_SEE_METADATA(name))
#else
namespace grpc_core {
namespace latent_see {
struct Flow {};
}  // namespace latent_see
}  // namespace grpc_core
#define GRPC_LATENT_SEE_PARENT_SCOPE(name) \
  do {                                     \
  } while (0)
#define GRPC_LATENT_SEE_SCOPE(name) \
  do {                              \
  } while (0)
#define GRPC_LATENT_SEE_MARK(name) \
  do {                             \
  } while (0)
#define GRPC_LATENT_SEE_FLOW(name) \
  grpc_core::latent_see::Flow {}
#endif

#endif
