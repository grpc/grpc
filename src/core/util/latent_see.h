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

#ifndef GRPC_SRC_CORE_UTIL_LATENT_SEE_H
#define GRPC_SRC_CORE_UTIL_LATENT_SEE_H

#include <grpc/support/port_platform.h>

#ifdef GRPC_ENABLE_LATENT_SEE
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "src/core/lib/gprpp/per_cpu.h"
#include "src/core/lib/gprpp/sync.h"

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

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static void Append(
      const Metadata* metadata, EventType type, uint64_t id) {
    thread_events_.push_back(
        Event{metadata, std::chrono::steady_clock::now(), id, type});
  }

 private:
  Log() = default;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static Log& Get() {
    static Log* log = []() {
      atexit([] {
        LOG(INFO) << "Writing latent_see.json in " << get_current_dir_name();
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

template <bool kFlush>
class Scope {
 public:
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION explicit Scope(const Metadata* metadata)
      : metadata_(metadata) {
    Log::Append(metadata_, EventType::kBegin, 0);
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION ~Scope() {
    Log::Append(metadata_, EventType::kEnd, 0);
    if (kFlush) Log::FlushThreadLog();
  }

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  const Metadata* const metadata_;
};

using ParentScope = Scope<true>;
using InnerScope = Scope<false>;

class Flow {
 public:
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Flow() : metadata_(nullptr) {}
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION explicit Flow(const Metadata* metadata)
      : metadata_(metadata),
        id_(next_flow_id_.fetch_add(1, std::memory_order_relaxed)) {
    Log::Append(metadata_, EventType::kFlowStart, id_);
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION ~Flow() {
    if (metadata_ != nullptr) {
      Log::Append(metadata_, EventType::kFlowEnd, id_);
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

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool is_active() const {
    return metadata_ != nullptr;
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void End() {
    if (metadata_ == nullptr) return;
    Log::Append(metadata_, EventType::kFlowEnd, id_);
    metadata_ = nullptr;
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void Begin(const Metadata* metadata) {
    if (metadata_ != nullptr) Log::Append(metadata_, EventType::kFlowEnd, id_);
    metadata_ = metadata;
    if (metadata_ == nullptr) return;
    id_ = next_flow_id_.fetch_add(1, std::memory_order_relaxed);
    Log::Append(metadata_, EventType::kFlowStart, id_);
  }

 private:
  const Metadata* metadata_;
  uint64_t id_;
  static std::atomic<uint64_t> next_flow_id_;
};

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline void Mark(const Metadata* md) {
  Log::Append(md, EventType::kMark, 0);
}

}  // namespace latent_see
}  // namespace grpc_core
#define GRPC_LATENT_SEE_METADATA(name)                                     \
  []() {                                                                   \
    static grpc_core::latent_see::Metadata metadata = {__FILE__, __LINE__, \
                                                       #name};             \
    return &metadata;                                                      \
  }()
// Parent scope: logs a begin and end event, and flushes the thread log on scope
// exit. Because the flush takes some time it's better to place one parent scope
// at the top of the stack, and use lighter weight scopes within it.
#define GRPC_LATENT_SEE_PARENT_SCOPE(name)                       \
  grpc_core::latent_see::ParentScope latent_see_scope##__LINE__( \
      GRPC_LATENT_SEE_METADATA(name))
// Inner scope: logs a begin and end event. Lighter weight than parent scope,
// but does not flush the thread state - so should only be enclosed by a parent
// scope.
#define GRPC_LATENT_SEE_INNER_SCOPE(name)                       \
  grpc_core::latent_see::InnerScope latent_see_scope##__LINE__( \
      GRPC_LATENT_SEE_METADATA(name))
// Mark: logs a single event.
// This is not flushed automatically, and so should only be used within a parent
// scope.
#define GRPC_LATENT_SEE_MARK(name) \
  grpc_core::latent_see::Mark(GRPC_LATENT_SEE_METADATA(name))
#else  // !def(GRPC_ENABLE_LATENT_SEE)
namespace grpc_core {
namespace latent_see {
struct Metadata {};
struct Flow {
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool is_active() const { return false; }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void End() {}
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void Begin(Metadata*) {}
};
}  // namespace latent_see
}  // namespace grpc_core
#define GRPC_LATENT_SEE_METADATA(name) nullptr
#define GRPC_LATENT_SEE_PARENT_SCOPE(name) \
  do {                                     \
  } while (0)
#define GRPC_LATENT_SEE_INNER_SCOPE(name) \
  do {                                    \
  } while (0)
#define GRPC_LATENT_SEE_MARK(name) \
  do {                             \
  } while (0)
#endif  // GRPC_ENABLE_LATENT_SEE

#endif  // GRPC_SRC_CORE_UTIL_LATENT_SEE_H
