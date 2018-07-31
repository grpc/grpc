/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_DEBUG_TRACE_H
#define GRPC_CORE_LIB_DEBUG_TRACE_H

#include <grpc/support/port_platform.h>

#include <grpc/support/atm.h>
#include <stdbool.h>

void grpc_tracer_init(const char* env_var_name);
void grpc_tracer_shutdown(void);

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define GRPC_THREADSAFE_TRACER
#endif
#endif

namespace grpc_core {

// Use the symbol GRPC_USE_TRACERS to determine if tracers will be enabled.
// The default in OSS is for tracers to be on since we support binary
// distributions of gRPC for the wrapped language (wr don't want to force
// recompilation to get tracing).
// Internally, however, for performance reasons, we compile them out by
// default (defined in BUILD), since internal build systems make recompiling
// trivial.
#ifndef GRPC_USE_TRACERS
#define GRPC_USE_TRACERS 1
#endif

#if GRPC_USE_TRACERS
class TraceFlag;
namespace testing {
void grpc_tracer_enable_flag(grpc_core::TraceFlag* flag);
}

class TraceFlag {
 public:
  TraceFlag(bool default_enabled, const char* name);
  ~TraceFlag() {}

  const char* name() const { return name_; }

  bool enabled() {
#ifdef GRPC_THREADSAFE_TRACER
    return gpr_atm_no_barrier_load(&value_) != 0;
#else
    return value_;
#endif  // GRPC_THREADSAFE_TRACER
  }

 private:
  friend void grpc_core::testing::grpc_tracer_enable_flag(TraceFlag* flag);
  friend class TraceFlagList;

  void set_enabled(bool enabled) {
#ifdef GRPC_THREADSAFE_TRACER
    gpr_atm_no_barrier_store(&value_, enabled);
#else
    value_ = enabled;
#endif
  }

  TraceFlag* next_tracer_;
  const char* const name_;
#ifdef GRPC_THREADSAFE_TRACER
  gpr_atm value_;
#else
  bool value_;
#endif
};

#else   // GRPC_USE_TRACERS
// Otherwise optimize away tracers.
class TraceFlag {
 public:
  constexpr TraceFlag(bool default_enabled, const char* name) {}
  const char* name() const { return "DisabledTracer"; }
  bool enabled() const { return false; }
};
#endif  // GRPC_USE_TRACERS

#if GRPC_USE_TRACERS
#if defined(NDEBUG)
// If we are using tracers, and opt build, use disabled DebugTracers.
class DebugOnlyTraceFlag : public TraceFlag {
 public:
  DebugOnlyTraceFlag(bool default_enabled, const char* name)
      : TraceFlag(false, "DisabledDebugTracer") {}
};
#else // defined(NDEBUG)
// If we are using tracers and a debug build, use Debugtracers.
class DebugOnlyTraceFlag : public TraceFlag {
 public:
  DebugOnlyTraceFlag(bool default_enabled, const char* name)
      : TraceFlag(default_enabled, name) {}
};
#endif  // defined(NDEBUG)
#else  // GRPC_USE_TRACERS
// If we are not using tracers, use constexpr DebugTracer for smallest
// compile.
class DebugOnlyTraceFlag : public TraceFlag {
 public:
  constexpr DebugOnlyTraceFlag(bool default_enabled, const char* name)
      : TraceFlag(false, "DisabledDebugTracer") {}
};
#endif  // GRPC_USE_TRACERS

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_DEBUG_TRACE_H */
