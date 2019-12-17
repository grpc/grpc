# Copyright 2017 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])  # Apache v2

grpc_package(
    name = "test/core/util",
    visibility = "public",
)

grpc_cc_library(
    name = "grpc_debugger_macros",
    srcs = [
        "debugger_macros.cc",
    ],
    hdrs = [
        "debugger_macros.h",
    ],
    deps = [
        "//:grpc_common",
    ],
)

grpc_cc_library(
    name = "grpc_test_util_base",
    srcs = [
        "cmdline.cc",
        "fuzzer_util.cc",
        "grpc_profiler.cc",
        "histogram.cc",
        "memory_counters.cc",
        "mock_endpoint.cc",
        "parse_hexstring.cc",
        "passthru_endpoint.cc",
        "port.cc",
        "port_isolated_runtime_environment.cc",
        "port_server_client.cc",
        "reconnect_server.cc",
        "slice_splitter.cc",
        "subprocess_posix.cc",
        "subprocess_windows.cc",
        "test_config.cc",
        "test_tcp_server.cc",
        "tracer_util.cc",
        "trickle_endpoint.cc",
    ],
    hdrs = [
        "cmdline.h",
        "fuzzer_util.h",
        "grpc_profiler.h",
        "histogram.h",
        "memory_counters.h",
        "mock_endpoint.h",
        "parse_hexstring.h",
        "passthru_endpoint.h",
        "port.h",
        "port_server_client.h",
        "reconnect_server.h",
        "slice_splitter.h",
        "subprocess.h",
        "test_config.h",
        "test_tcp_server.h",
        "tracer_util.h",
        "trickle_endpoint.h",
    ],
    data = [
        "lsan_suppressions.txt",
        "tsan_suppressions.txt",
        "ubsan_suppressions.txt",
    ],
    language = "C++",
    deps = [
        ":grpc_debugger_macros",
        "//:gpr",
        "//:grpc_common",
    ],
)

grpc_cc_library(
    name = "grpc_test_util",
    srcs = [],
    hdrs = [],
    language = "C++",
    deps = [
        ":grpc_test_util_base",
        "//:grpc",
    ],
)

grpc_cc_library(
    name = "grpc_test_util_unsecure",
    srcs = [],
    hdrs = [],
    language = "C++",
    deps = [
        ":grpc_test_util_base",
        "//:grpc_unsecure",
    ],
)

grpc_cc_test(
    name = "cmdline_test",
    srcs = ["cmdline_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        ":grpc_test_util",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "fuzzer_corpus_test",
    testonly = 1,
    srcs = ["fuzzer_corpus_test.cc"],
    external_deps = [
        "gtest",
        "gflags",
    ],
    tags = ["no_windows"],
    deps = [
        ":grpc_test_util",
        "//:grpc",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_test(
    name = "histogram_test",
    srcs = ["histogram_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        ":grpc_test_util",
        "//:gpr",
    ],
)

sh_library(
    name = "fuzzer_one_entry_runner",
    srcs = ["fuzzer_one_entry_runner.sh"],
)

sh_library(
    name = "run_with_poller_sh",
    srcs = ["run_with_poller.sh"],
)

grpc_cc_library(
    name = "test_lb_policies",
    testonly = 1,
    srcs = ["test_lb_policies.cc"],
    hdrs = ["test_lb_policies.h"],
    deps = [
        "//:grpc",
    ],
)
