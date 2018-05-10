/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <gflags/gflags.h>
#include <string>
#include <thread>
#include <vector>

#include "test/cpp/util/subprocess.h"
#include "test/cpp/util/test_config.h"

#include "src/core/lib/gpr/env.h"
#include "test/core/util/port.h"

DEFINE_bool(
    running_under_bazel, false,
    "True if this test is running under bazel. "
    "False indicates that this test is running under run_tests.py. "
    "Child process test binaries are located differently based on this flag. ");

DEFINE_string(test_bin_name, "",
              "Name, without the preceding path, of the test binary");

DEFINE_string(grpc_test_directory_relative_to_test_srcdir,
              "/com_github_grpc_grpc",
              "This flag only applies if runner_under_bazel is true. This "
              "flag is ignored if runner_under_bazel is false. "
              "Directory of the <repo-root>/test directory relative to bazel's "
              "TEST_SRCDIR environment variable");

using grpc::SubProcess;

static volatile sig_atomic_t abort_wait_for_child = 0;

static void sighandler(int sig) { abort_wait_for_child = 1; }

static void register_sighandler() {
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = sighandler;
  sigaction(SIGINT, &act, nullptr);
  sigaction(SIGTERM, &act, nullptr);
}

namespace {

const int kTestTimeoutSeconds = 60 * 2;

void RunSigHandlingThread(SubProcess* test_driver, gpr_mu* test_driver_mu,
                          gpr_cv* test_driver_cv, int* test_driver_done) {
  gpr_timespec overall_deadline =
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                   gpr_time_from_seconds(kTestTimeoutSeconds, GPR_TIMESPAN));
  while (true) {
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    if (gpr_time_cmp(now, overall_deadline) > 0 || abort_wait_for_child) break;
    gpr_mu_lock(test_driver_mu);
    if (*test_driver_done) {
      gpr_mu_unlock(test_driver_mu);
      return;
    }
    gpr_timespec wait_deadline = gpr_time_add(
        gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_seconds(1, GPR_TIMESPAN));
    gpr_cv_wait(test_driver_cv, test_driver_mu, wait_deadline);
    gpr_mu_unlock(test_driver_mu);
  }
  gpr_log(GPR_DEBUG,
          "Test timeout reached or received signal. Interrupting test driver "
          "child process.");
  test_driver->Interrupt();
  return;
}
}  // namespace

namespace grpc {

namespace testing {

void InvokeResolverComponentTestsRunner(std::string test_runner_bin_path,
                                        std::string test_bin_path,
                                        std::string dns_server_bin_path,
                                        std::string records_config_path,
                                        std::string dns_resolver_bin_path,
                                        std::string tcp_connect_bin_path) {
  int dns_server_port = grpc_pick_unused_port_or_die();

  SubProcess* test_driver =
      new SubProcess({test_runner_bin_path, "--test_bin_path=" + test_bin_path,
                      "--dns_server_bin_path=" + dns_server_bin_path,
                      "--records_config_path=" + records_config_path,
                      "--dns_server_port=" + std::to_string(dns_server_port),
                      "--dns_resolver_bin_path=" + dns_resolver_bin_path,
                      "--tcp_connect_bin_path=" + tcp_connect_bin_path});
  gpr_mu test_driver_mu;
  gpr_mu_init(&test_driver_mu);
  gpr_cv test_driver_cv;
  gpr_cv_init(&test_driver_cv);
  int test_driver_done = 0;
  register_sighandler();
  std::thread sig_handling_thread(RunSigHandlingThread, test_driver,
                                  &test_driver_mu, &test_driver_cv,
                                  &test_driver_done);
  int status = test_driver->Join();
  if (WIFEXITED(status)) {
    if (WEXITSTATUS(status)) {
      gpr_log(GPR_INFO,
              "Resolver component test test-runner exited with code %d",
              WEXITSTATUS(status));
      abort();
    }
  } else if (WIFSIGNALED(status)) {
    gpr_log(GPR_INFO,
            "Resolver component test test-runner ended from signal %d",
            WTERMSIG(status));
    abort();
  } else {
    gpr_log(GPR_INFO,
            "Resolver component test test-runner ended with unknown status %d",
            status);
    abort();
  }
  gpr_mu_lock(&test_driver_mu);
  test_driver_done = 1;
  gpr_cv_signal(&test_driver_cv);
  gpr_mu_unlock(&test_driver_mu);
  sig_handling_thread.join();
  delete test_driver;
  gpr_mu_destroy(&test_driver_mu);
  gpr_cv_destroy(&test_driver_cv);
}

}  // namespace testing

}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  grpc_init();
  GPR_ASSERT(FLAGS_test_bin_name != "");
  std::string my_bin = argv[0];
  if (FLAGS_running_under_bazel) {
    GPR_ASSERT(FLAGS_grpc_test_directory_relative_to_test_srcdir != "");
    // Use bazel's TEST_SRCDIR environment variable to locate the "test data"
    // binaries.
    char* test_srcdir = gpr_getenv("TEST_SRCDIR");
    std::string const bin_dir =
        test_srcdir + FLAGS_grpc_test_directory_relative_to_test_srcdir +
        std::string("/test/cpp/naming");
    // Invoke bazel's executeable links to the .sh and .py scripts (don't use
    // the .sh and .py suffixes) to make
    // sure that we're using bazel's test environment.
    grpc::testing::InvokeResolverComponentTestsRunner(
        bin_dir + "/resolver_component_tests_runner",
        bin_dir + "/" + FLAGS_test_bin_name, bin_dir + "/utils/dns_server",
        bin_dir + "/resolver_test_record_groups.yaml",
        bin_dir + "/utils/dns_resolver", bin_dir + "/utils/tcp_connect");
    gpr_free(test_srcdir);
  } else {
    // Get the current binary's directory relative to repo root to invoke the
    // correct build config (asan/tsan/dbg, etc.).
    std::string const bin_dir = my_bin.substr(0, my_bin.rfind('/'));
    // Invoke the .sh and .py scripts directly where they are in source code.
    grpc::testing::InvokeResolverComponentTestsRunner(
        "test/cpp/naming/resolver_component_tests_runner.sh",
        bin_dir + "/" + FLAGS_test_bin_name,
        "test/cpp/naming/utils/dns_server.py",
        "test/cpp/naming/resolver_test_record_groups.yaml",
        "test/cpp/naming/utils/dns_resolver.py",
        "test/cpp/naming/utils/tcp_connect.py");
  }
  grpc_shutdown();
  return 0;
}
