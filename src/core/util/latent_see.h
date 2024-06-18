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

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

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

enum class EventType : uint8_t { kBegin, kEnd, kFlowStart, kFlowEnd, kMark };

class Log {
 public:
  static void FlushThreadLog();

  static void Append(const Metadata* metadata, EventType type, uint64_t id) {
    thread_events_.push_back(
        Event{metadata, std::chrono::steady_clock::now(), id, type});
  }

 private:
  Log() = default;

  static Log& Get() {
    static Log* log = []() {
      atexit([] {
        FILE* f = fopen("latent_see.json", "w");
        if (f == nullptr) return;
        fprintf(f, "%s", log->GenerateJson().c_str());
        fclose(f);
      });
      return new Log();
    }();
    return *log;
  }

  std::string GenerateJson();

  struct Event {
    const Metadata* metadata;
    std::chrono::steady_clock::time_point timestamp;
    uint64_t id;
    EventType type;
  };
  struct RecordedEvent {
    uint64_t thread_id;
    uint64_t batch_id;
    Event event;
  };
  std::atomic<uint64_t> next_thread_id_{1};
  std::atomic<uint64_t> next_batch_id_{1};
  static thread_local std::vector<Event> thread_events_;
  static thread_local uint64_t thread_id_;
  struct Fragment {
    Mutex mu;
    std::vector<RecordedEvent> events ABSL_GUARDED_BY(mu);
  };
  PerCpu<Fragment> fragments_{PerCpuOptions()};
};

class Scope {
 public:
  explicit Scope(const Metadata* metadata) : metadata_(metadata) {
    Log::Append(metadata_, EventType::kBegin, 0);
  }
  ~Scope() { Log::Append(metadata_, EventType::kEnd, 0); }

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  const Metadata* const metadata_;
};

class ParentScope {
 public:
  explicit ParentScope(const Metadata* metadata) : metadata_(metadata) {
    Log::Append(metadata_, EventType::kBegin, 0);
  }
  ~ParentScope() {
    Log::Append(metadata_, EventType::kEnd, 0);
    Log::FlushThreadLog();
  }

  ParentScope(const Scope&) = delete;
  ParentScope& operator=(const Scope&) = delete;

 private:
  const Metadata* const metadata_;
};

class Flow {
 public:
  explicit Flow(const Metadata* metadata) : metadata_(metadata) {
    Log::Append(metadata_, EventType::kFlowStart, id_);
    Log::FlushThreadLog();
  }
  ~Flow() {
    if (metadata_ != nullptr) {
      Log::Append(metadata_, EventType::kFlowEnd, id_);
      Log::FlushThreadLog();
    }
  }

  Flow(const Flow&) = delete;
  Flow& operator=(const Flow&) = delete;
  Flow(Flow&& other) noexcept
      : metadata_(std::exchange(other.metadata_, nullptr)), id_(other.id_) {}
  Flow& operator=(Flow&& other) noexcept {
    if (metadata_ != nullptr) Log::Append(metadata_, EventType::kFlowEnd, id_);
    metadata_ = std::exchange(other.metadata_, nullptr);
    id_ = other.id_;
    return *this;
  }

 private:
  const Metadata* metadata_;
  uint64_t id_{next_flow_id_.fetch_add(1, std::memory_order_relaxed)};
  static std::atomic<uint64_t> next_flow_id_;
};
}  // namespace latent_see
}  // namespace grpc_core
#define GRPC_LATENT_SEE_METADATA(name)                                     \
  []() {                                                                   \
    static grpc_core::latent_see::Metadata metadata = {__FILE__, __LINE__, \
                                                       #name};             \
    return &metadata;                                                      \
  }()
#define GRPC_LATENT_SEE_PARENT_SCOPE(name)                       \
  grpc_core::latent_see::ParentScope latent_see_scope##__LINE__( \
      GRPC_LATENT_SEE_METADATA(name))
#define GRPC_LATENT_SEE_SCOPE(name)                        \
  grpc_core::latent_see::Scope latent_see_scope##__LINE__( \
      GRPC_LATENT_SEE_METADATA(name))
#define GRPC_LATENT_SEE_MARK(name) \
  grpc_core::latent_see::Mark(GRPC_LATENT_SEE_METADATA(name))
#define GRPC_LATENT_SEE_FLOW(name) \
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
