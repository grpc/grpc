//
//
// Copyright 2015 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_DEBUG_TRACE_H
#define GRPC_SRC_CORE_LIB_DEBUG_TRACE_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <map>
#include <string>

#include "absl/strings/string_view.h"

void grpc_tracer_init();
void grpc_tracer_shutdown(void);

namespace grpc_core {

class TraceFlag;
class TraceFlagList {
 public:
  static bool Set(absl::string_view name, bool enabled);
  static void Add(TraceFlag* flag);
  static void SaveTo(std::map<std::string, bool>& values);

 private:
  static void LogAllTracers();
  static TraceFlag* root_tracer_;
};

namespace testing {
void grpc_tracer_enable_flag(TraceFlag* flag);
}

class TraceFlag {
 public:
  TraceFlag(bool default_enabled, const char* name);
  // TraceFlag needs to be trivially destructible since it is used as global
  // variable.
  ~TraceFlag() = default;

  const char* name() const { return name_; }

// Use the symbol GRPC_USE_TRACERS to determine if tracers will be enabled in
// opt builds (tracers are always on in dbg builds). The default in OSS is for
// tracers to be on since we support binary distributions of gRPC for the
// wrapped language (wr don't want to force recompilation to get tracing).
// Internally, however, for performance reasons, we compile them out by
// default, since internal build systems make recompiling trivial.
//
// Prefer GRPC_TRACE_FLAG_ENABLED() macro instead of using enabled() directly.
#define GRPC_USE_TRACERS  // tracers on by default in OSS
#if defined(GRPC_USE_TRACERS) || !defined(NDEBUG)
  bool enabled() { return value_.load(std::memory_order_relaxed); }
#else
  bool enabled() { return false; }
#endif  // defined(GRPC_USE_TRACERS) || !defined(NDEBUG)

 private:
  friend void testing::grpc_tracer_enable_flag(TraceFlag* flag);
  friend class TraceFlagList;

  void set_enabled(bool enabled) {
    value_.store(enabled, std::memory_order_relaxed);
  }

  TraceFlag* next_tracer_;
  const char* const name_;
  std::atomic<bool> value_;
};

#define GRPC_TRACE_FLAG_ENABLED(f) GPR_UNLIKELY((f).enabled())

#ifndef NDEBUG
typedef TraceFlag DebugOnlyTraceFlag;
#else
class DebugOnlyTraceFlag {
 public:
  constexpr DebugOnlyTraceFlag(bool /*default_enabled*/, const char* /*name*/) {
  }
  constexpr bool enabled() const { return false; }
  constexpr const char* name() const { return "DebugOnlyTraceFlag"; }

 private:
  void set_enabled(bool /*enabled*/) {}
};
#endif

class SavedTraceFlags {
 public:
  SavedTraceFlags();
  void Restore();

 private:
  std::map<std::string, bool> values_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_DEBUG_TRACE_H
