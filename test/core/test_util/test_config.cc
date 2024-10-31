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

#include "test/core/test_util/test_config.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <inttypes.h>
#include <stdlib.h>

#include <mutex>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/surface/init.h"
#include "src/core/util/crash.h"
#include "test/core/event_engine/test_init.h"
#include "test/core/test_util/build.h"
#include "test/core/test_util/stack_tracer.h"

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

#ifdef GPR_WINDOWS
// clang-format off
#include <winsock2.h>
#include <iphlpapi.h>
// clang-format on
#endif

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

namespace {
void RmArg(int i, int* argc, char** argv) {
  --(*argc);
  while (i < *argc) {
    argv[i] = argv[i + 1];
    ++i;
  }
}

void ParseTestArgs(int* argc, char** argv) {
  if (argc == nullptr || *argc <= 1) return;
  // flags to look for and consume
  const absl::string_view engine_flag{"--engine="};
  int i = 1;
  while (i < *argc) {
    if (absl::StartsWith(argv[i], engine_flag)) {
      absl::Status engine_set =
          grpc_event_engine::experimental::InitializeTestingEventEngineFactory(
              argv[i] + engine_flag.length());
      if (!engine_set.ok()) {
        grpc_core::Crash(absl::StrFormat("%s", engine_set.ToString().c_str()));
      }
      // remove the spent argv
      RmArg(i, argc, argv);
      continue;
    }
    ++i;
  }
}

// grpc-oss-only-begin
std::once_flag log_flag;
// grpc-oss-only-end

}  // namespace

void grpc_test_init(int* argc, char** argv) {
  // grpc-oss-only-begin
  std::call_once(log_flag, []() { absl::InitializeLog(); });
  absl::SetGlobalVLogLevel(2);
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  // grpc-oss-only-end
  gpr_log_verbosity_init();
  ParseTestArgs(argc, argv);
  grpc_core::testing::InitializeStackTracer(argv[0]);
  absl::FailureSignalHandlerOptions options;
  absl::InstallFailureSignalHandler(options);
  VLOG(2) << "test slowdown factor: sanitizer="
          << grpc_test_sanitizer_slowdown_factor()
          << ", fixture=" << g_fixture_slowdown_factor
          << ", poller=" << g_poller_slowdown_factor
          << ", total=" << grpc_test_slowdown_factor();
  // seed rng with pid, so we don't end up with the same random numbers as a
  // concurrently running test binary
  srand(seed());
}

void grpc_set_absl_verbosity_debug() {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  absl::SetVLogLevel("*grpc*/*", 2);
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

void grpc_disable_all_absl_logs() {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfinity);
  absl::SetVLogLevel("*grpc*/*", -1);
}

void grpc_prewarm_os_for_tests() {
#ifdef GPR_WINDOWS
  // On Windows RBE, c-ares' ares_init_options which internally calls
  // GetAdaptersAddresses sometimes take >20s to return causing tests to
  // timeout. This is a hack to prewarm the cache by calling that function
  // during test setup.
#define IPAA_INITIAL_BUF_SZ 15 * 1024
  ULONG AddrFlags = 0;
  ULONG Bufsz = IPAA_INITIAL_BUF_SZ;
  ULONG ReqBufsz = IPAA_INITIAL_BUF_SZ;
  IP_ADAPTER_ADDRESSES* ipaa;
  ipaa = static_cast<IP_ADAPTER_ADDRESSES*>(malloc(Bufsz));
  GetAdaptersAddresses(AF_UNSPEC, AddrFlags, NULL, ipaa, &ReqBufsz);
  free(ipaa);
#endif
}

namespace grpc {
namespace testing {

TestEnvironment::TestEnvironment(int* argc, char** argv) {
  grpc_test_init(argc, argv);
}

TestEnvironment::~TestEnvironment() {
  // This will wait until gRPC shutdown has actually happened to make sure
  // no gRPC resources (such as thread) are active. (timeout = 10s)
  if (!grpc_wait_until_shutdown(10)) {
    LOG(ERROR) << "Timeout in waiting for gRPC shutdown";
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
  LOG(INFO) << "TestEnvironment ends";
}

TestGrpcScope::TestGrpcScope() { grpc_init(); }

TestGrpcScope::~TestGrpcScope() {
  grpc_shutdown();
  if (!grpc_wait_until_shutdown(10)) {
    LOG(ERROR) << "Timeout in waiting for gRPC shutdown";
  }
}

}  // namespace testing
}  // namespace grpc
