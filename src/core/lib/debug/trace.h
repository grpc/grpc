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

#include <grpc/support/atm.h>
#include <grpc/support/port_platform.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void grpc_tracer_init(const char* env_var_name);
void grpc_tracer_shutdown(void);

#ifdef __cplusplus
}
#endif

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define GRPC_THREADSAFE_TRACER
#endif
#endif

#ifdef __cplusplus

namespace grpc_core {

class TraceFlag;
class TraceFlagList {
 public:
  static bool Set(const char* name, bool enabled);
  static void Add(TraceFlag* flag);

 private:
  static void LogAllTracers();
  static TraceFlag* root_tracer_;
};

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
#endif
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

#ifndef NDEBUG
typedef TraceFlag DebugOnlyTraceFlag;
#else
class DebugOnlyTraceFlag {
 public:
  DebugOnlyTraceFlag(bool default_enabled, const char* name) {}
  bool enabled() { return false; }

 private:
  void set_enabled(bool enabled) {}
};
#endif

}  // namespace grpc_core

#endif  // __cplusplus

#endif /* GRPC_CORE_LIB_DEBUG_TRACE_H */
