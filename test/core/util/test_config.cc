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

#include "test/core/util/test_config.h"

#include <grpc/impl/codegen/gpr_types.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/examine_stack.h"
#include "src/core/lib/surface/init.h"
#include "test/core/util/stack_tracer.h"

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"

int64_t g_fixture_slowdown_factor = 1;
int64_t g_poller_slowdown_factor = 1;

#if GPR_GETPID_IN_UNISTD_H
#include <unistd.h>
static unsigned seed(void) { return static_cast<unsigned>(getpid()); }
#endif

#if GPR_GETPID_IN_PROCESS_H
#include <process.h>
static unsigned seed(void) { return (unsigned)_getpid(); }
#endif

bool BuiltUnderValgrind() {
#ifdef RUNNING_ON_VALGRIND
  return true;
#else
  return false;
#endif
}

bool BuiltUnderTsan() {
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
  return true;
#else
  return false;
#endif
#else
#ifdef THREAD_SANITIZER
  return true;
#else
  return false;
#endif
#endif
}

bool BuiltUnderAsan() {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
  return true;
#else
  return false;
#endif
#else
#ifdef ADDRESS_SANITIZER
  return true;
#else
  return false;
#endif
#endif
}

bool BuiltUnderMsan() {
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
  return true;
#else
  return false;
#endif
#else
#ifdef MEMORY_SANITIZER
  return true;
#else
  return false;
#endif
#endif
}

bool BuiltUnderUbsan() {
#ifdef GRPC_UBSAN
  return true;
#else
  return false;
#endif
}

int64_t grpc_test_sanitizer_slowdown_factor() {
  int64_t sanitizer_multiplier = 1;
  if (BuiltUnderValgrind()) {
    sanitizer_multiplier = 20;
  } else if (BuiltUnderTsan()) {
    sanitizer_multiplier = 5;
  } else if (BuiltUnderAsan()) {
    sanitizer_multiplier = 3;
  } else if (BuiltUnderMsan()) {
    sanitizer_multiplier = 4;
  } else if (BuiltUnderUbsan()) {
    sanitizer_multiplier = 5;
  }
  return sanitizer_multiplier;
}

int64_t grpc_test_slowdown_factor() {
  return grpc_test_sanitizer_slowdown_factor() * g_fixture_slowdown_factor *
         g_poller_slowdown_factor;
}

gpr_timespec grpc_timeout_seconds_to_deadline(int64_t time_s) {
  return gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_millis(
          grpc_test_slowdown_factor() * static_cast<int64_t>(1e3) * time_s,
          GPR_TIMESPAN));
}

gpr_timespec grpc_timeout_milliseconds_to_deadline(int64_t time_ms) {
  return gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_micros(
          grpc_test_slowdown_factor() * static_cast<int64_t>(1e3) * time_ms,
          GPR_TIMESPAN));
}

void grpc_test_init(int /*argc*/, char** argv) {
  grpc_core::testing::InitializeStackTracer(argv[0]);
  absl::FailureSignalHandlerOptions options;
  absl::InstallFailureSignalHandler(options);
  gpr_log_verbosity_init();
  gpr_log(GPR_DEBUG,
          "test slowdown factor: sanitizer=%" PRId64 ", fixture=%" PRId64
          ", poller=%" PRId64 ", total=%" PRId64,
          grpc_test_sanitizer_slowdown_factor(), g_fixture_slowdown_factor,
          g_poller_slowdown_factor, grpc_test_slowdown_factor());
  /* seed rng with pid, so we don't end up with the same random numbers as a
     concurrently running test binary */
  srand(seed());
}

bool grpc_wait_until_shutdown(int64_t time_s) {
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(time_s);
  while (grpc_is_initialized()) {
    grpc_maybe_wait_for_async_shutdown();
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_millis(1, GPR_TIMESPAN)));
    if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) > 0) {
      return false;
    }
  }
  return true;
}

namespace grpc {
namespace testing {

TestEnvironment::TestEnvironment(int argc, char** argv) {
  grpc_test_init(argc, argv);
}

TestEnvironment::~TestEnvironment() {
  // This will wait until gRPC shutdown has actually happened to make sure
  // no gRPC resources (such as thread) are active. (timeout = 10s)
  if (!grpc_wait_until_shutdown(10)) {
    gpr_log(GPR_ERROR, "Timeout in waiting for gRPC shutdown");
  }
  if (BuiltUnderMsan()) {
    // This is a workaround for MSAN. MSAN doesn't like having shutdown thread
    // running. Although the code above waits until shutdown is done, chances
    // are that thread itself is still alive. To workaround this problem, this
    // is going to wait for 0.5 sec to give a chance to the shutdown thread to
    // exit. https://github.com/grpc/grpc/issues/23695
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_millis(500, GPR_TIMESPAN)));
  }
  gpr_log(GPR_INFO, "TestEnvironment ends");
}

TestGrpcScope::TestGrpcScope() { grpc_init(); }

TestGrpcScope::~TestGrpcScope() {
  grpc_shutdown();
  if (!grpc_wait_until_shutdown(10)) {
    gpr_log(GPR_ERROR, "Timeout in waiting for gRPC shutdown");
  }
}

}  // namespace testing
}  // namespace grpc
