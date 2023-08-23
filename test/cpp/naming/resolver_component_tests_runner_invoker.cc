// Copyright 2017 The gRPC Authors.
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

#include <grpc/support/port_platform.h>

#include <signal.h>
#include <string.h>

#ifndef GPR_WINDOWS
#include <unistd.h>
#endif  // GPR_WINDOWS

#include <memory>
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
#ifdef GPR_WINDOWS
#include "test/cpp/util/windows/manifest_file.h"
#endif  // GPR_WINDOWS

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

namespace grpc {

namespace testing {

int InvokeResolverComponentTestsRunner(
    std::string test_runner_bin_path, const std::string& test_bin_path,
    const std::string& dns_server_bin_path,
    const std::string& records_config_path,
    const std::string& dns_resolver_bin_path,
    const std::string& tcp_connect_bin_path) {
  int dns_server_port = grpc_pick_unused_port_or_die();
  auto test_driver = std::make_unique<SubProcess>(std::vector<std::string>(
      {std::move(test_runner_bin_path), "--test_bin_path=" + test_bin_path,
       "--dns_server_bin_path=" + dns_server_bin_path,
       "--records_config_path=" + records_config_path,
       "--dns_server_port=" + std::to_string(dns_server_port),
       "--dns_resolver_bin_path=" + dns_resolver_bin_path,
       "--tcp_connect_bin_path=" + tcp_connect_bin_path,
       "--extra_args=" + absl::GetFlag(FLAGS_extra_args)}));
  return test_driver->Join();
}

}  // namespace testing

}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  grpc_init();
  GPR_ASSERT(!absl::GetFlag(FLAGS_test_bin_name).empty());
  std::string my_bin = argv[0];
  int result = 0;
  if (absl::GetFlag(FLAGS_running_under_bazel)) {
    GPR_ASSERT(!absl::GetFlag(FLAGS_grpc_test_directory_relative_to_test_srcdir)
                    .empty());
    // Use bazel's TEST_SRCDIR environment variable to locate the "test data"
    // binaries.
    auto test_srcdir = grpc_core::GetEnv("TEST_SRCDIR");
#ifndef GPR_WINDOWS
    std::string const bin_dir =
        test_srcdir.value() +
        absl::GetFlag(FLAGS_grpc_test_directory_relative_to_test_srcdir) +
        std::string("/test/cpp/naming");
    // Invoke bazel's executeable links to the .sh and .py scripts (don't use
    // the .sh and .py suffixes) to make
    // sure that we're using bazel's test environment.
    result = grpc::testing::InvokeResolverComponentTestsRunner(
        bin_dir + "/resolver_component_tests_runner",
        bin_dir + "/" + absl::GetFlag(FLAGS_test_bin_name),
        bin_dir + "/utils/dns_server",
        bin_dir + "/resolver_test_record_groups.yaml",
        bin_dir + "/utils/dns_resolver", bin_dir + "/utils/tcp_connect");
#else
#ifndef GRPC_PORT_ISOLATED_RUNTIME
    gpr_log(GPR_ERROR,
            "You are invoking the test locally with Bazel, you may need to "
            "invoke Bazel with --enable_runfiles=yes.");
#endif  // GRPC_PORT_ISOLATED_RUNTIME
    result = grpc::testing::InvokeResolverComponentTestsRunner(
        grpc::testing::NormalizeFilePath(
            test_srcdir.value() + "/com_github_grpc_grpc/test/cpp/naming/"
                                  "resolver_component_tests_runner.exe"),
        grpc::testing::NormalizeFilePath(
            test_srcdir.value() + "/com_github_grpc_grpc/test/cpp/naming/" +
            absl::GetFlag(FLAGS_test_bin_name) + ".exe"),
        grpc::testing::NormalizeFilePath(
            test_srcdir.value() +
            "/com_github_grpc_grpc/test/cpp/naming/utils/dns_server.exe"),
        grpc::testing::NormalizeFilePath(
            test_srcdir.value() + "/com_github_grpc_grpc/test/cpp/naming/"
                                  "resolver_test_record_groups.yaml"),
        grpc::testing::NormalizeFilePath(
            test_srcdir.value() +
            "/com_github_grpc_grpc/test/cpp/naming/utils/dns_resolver.exe"),
        grpc::testing::NormalizeFilePath(
            test_srcdir.value() +
            "/com_github_grpc_grpc/test/cpp/naming/utils/tcp_connect.exe"));
#endif  // GPR_WINDOWS
  } else {
#ifdef GPR_WINDOWS
    grpc_core::Crash(
        "Resolver component tests runner invoker does not support running "
        "without Bazel on Windows for now.");
#endif  // GPR_WINDOWS
    // Get the current binary's directory relative to repo root to invoke the
    // correct build config (asan/tsan/dbg, etc.).
    std::string const bin_dir = my_bin.substr(0, my_bin.rfind('/'));
    // Invoke the .sh and .py scripts directly where they are in source code.
    result = grpc::testing::InvokeResolverComponentTestsRunner(
        "test/cpp/naming/resolver_component_tests_runner.py",
        bin_dir + "/" + absl::GetFlag(FLAGS_test_bin_name),
        "test/cpp/naming/utils/dns_server.py",
        "test/cpp/naming/resolver_test_record_groups.yaml",
        "test/cpp/naming/utils/dns_resolver.py",
        "test/cpp/naming/utils/tcp_connect.py");
  }
  grpc_shutdown();
  return result;
}
