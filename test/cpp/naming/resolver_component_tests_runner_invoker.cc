//
//
// Copyright 2017 gRPC authors.
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

#include <signal.h>
#include <string.h>

#include <string>
#include <thread>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/crash.h"

#ifdef __FreeBSD__
#include <sys/wait.h>
#endif

#include "src/core/lib/gprpp/env.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/subprocess.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(
    bool, running_under_bazel, false,
    "True if this test is running under bazel. "
    "False indicates that this test is running under run_tests.py. "
    "Child process test binaries are located differently based on this flag. ");

ABSL_FLAG(std::string, test_bin_name, "",
          "Name, without the preceding path, of the test binary");

ABSL_FLAG(std::string, grpc_test_directory_relative_to_test_srcdir,
          "/com_github_grpc_grpc",
          "This flag only applies if runner_under_bazel is true. This "
          "flag is ignored if runner_under_bazel is false. "
          "Directory of the <repo-root>/test directory relative to bazel's "
          "TEST_SRCDIR environment variable");

ABSL_FLAG(std::string, extra_args, "",
          "Comma-separated list of opaque command args to plumb through to "
          "the binary pointed at by --test_bin_name");

namespace {

int InvokeResolverComponentTestsRunner(
    std::string test_runner_bin_path, const std::string& test_bin_path,
    const std::string& dns_server_bin_path,
    const std::string& records_config_path,
    const std::string& dns_resolver_bin_path,
    const std::string& tcp_connect_bin_path) {
  int dns_server_port = grpc_pick_unused_port_or_die();
  grpc::SubProcess* test_driver = new grpc;:SubProcess(
      {std::move(test_runner_bin_path), "--test_bin_path=" + test_bin_path,
       "--dns_server_bin_path=" + dns_server_bin_path,
       "--records_config_path=" + records_config_path,
       "--dns_server_port=" + std::to_string(dns_server_port),
       "--dns_resolver_bin_path=" + dns_resolver_bin_path,
       "--tcp_connect_bin_path=" + tcp_connect_bin_path,
       "--extra_args=" + absl::GetFlag(FLAGS_extra_args)});
  int status = test_driver->Join();
  delete test_driver;
  return status;
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  grpc_init();
  GPR_ASSERT(!absl::GetFlag(FLAGS_test_bin_name).empty());
  std::string full_test_bin_name = absl::GetFlag(FLAGS_test_bin_name);
#ifdef GPR_WINDOWS
  full_test_bin_name = full_test_bin_name + ".exe";
#endif
  GPR_ASSERT(!absl::GetFlag(FLAGS_test_bin_name).empty());
  std::string my_bin = argv[0];
  int status;
  if (absl::GetFlag(FLAGS_running_under_bazel)) {
    GPR_ASSERT(!absl::GetFlag(FLAGS_grpc_test_directory_relative_to_test_srcdir)
                    .empty());
    // Use bazel's TEST_SRCDIR environment variable to locate the "test data"
    // binaries.
    auto test_srcdir = grpc_core::GetEnv("TEST_SRCDIR");
    std::string const bin_dir =
        test_srcdir.value() +
        absl::GetFlag(FLAGS_grpc_test_directory_relative_to_test_srcdir) +
        std::string("/test/cpp/naming");
    // Invoke bazel's executeable links to the .sh and .py scripts (don't use
    // the .sh and .py suffixes) to make
    // sure that we're using bazel's test environment.
    status = grpc::testing::InvokeResolverComponentTestsRunner(
        bin_dir + "/resolver_component_tests_runner",
        bin_dir + "/" + full_test_bin_name, bin_dir + "/utils/dns_server",
        bin_dir + "/resolver_test_record_groups.yaml",
        bin_dir + "/utils/dns_resolver", bin_dir + "/utils/tcp_connect");
  } else {
    // Get the current binary's directory relative to repo root to invoke the
    // correct build config (asan/tsan/dbg, etc.).
    std::string const bin_dir = my_bin.substr(0, my_bin.rfind('/'));
    // Invoke the .sh and .py scripts directly where they are in source code.
    status = grpc::testing::InvokeResolverComponentTestsRunner(
        "test/cpp/naming/resolver_component_tests_runner.py",
        bin_dir + "/" + full_test_bin_name,
        "test/cpp/naming/utils/dns_server.py",
        "test/cpp/naming/resolver_test_record_groups.yaml",
        "test/cpp/naming/utils/dns_resolver.py",
        "test/cpp/naming/utils/tcp_connect.py");
  }
  grpc_shutdown();
  return status;
}
