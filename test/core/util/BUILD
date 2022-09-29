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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/core/util",
    visibility = "public",
)

grpc_cc_library(
    name = "grpc_suppressions",
    data = [
        "lsan_suppressions.txt",
        "tsan_suppressions.txt",
        "ubsan_suppressions.txt",
    ],
)

TEST_UTILS_THAT_USE_GRPC_H_SOURCES = [
    "port.cc",
    "port_isolated_runtime_environment.cc",
    "port_server_client.cc",
    "reconnect_server.cc",
    "test_config.cc",
    "test_tcp_server.cc",
]

TEST_UTILS_THAT_USE_GRPC_H_HEADERS = [
    "port.h",
    "reconnect_server.h",
    "port_server_client.h",
    "test_config.h",
    "test_tcp_server.h",
]

grpc_cc_library(
    name = "grpc_test_util_base",
    srcs = [
        "cmdline.cc",
        "fuzzer_util.cc",
        "grpc_profiler.cc",
        "histogram.cc",
        "mock_endpoint.cc",
        "parse_hexstring.cc",
        "passthru_endpoint.cc",
        "resolve_localhost_ip46.cc",
        "slice_splitter.cc",
        "subprocess_posix.cc",
        "subprocess_windows.cc",
        "tracer_util.cc",
    ],
    hdrs = [
        "cmdline.h",
        "evaluate_args_test_util.h",
        "fuzzer_util.h",
        "grpc_profiler.h",
        "histogram.h",
        "mock_authorization_endpoint.h",
        "mock_endpoint.h",
        "parse_hexstring.h",
        "passthru_endpoint.h",
        "resolve_localhost_ip46.h",
        "slice_splitter.h",
        "subprocess.h",
        "tracer_util.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:span",
    ],
    language = "C++",
    tags = ["nofixdeps"],
    deps = [
        "//:arena",
        "//:debug_location",
        "//:gpr",
        "//:grpc_authorization_base",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:grpc_sockaddr",
        "//:grpc_trace",
        "//:iomgr_fwd",
        "//:iomgr_timer",
        "//:memory_quota",
        "//:ref_counted_ptr",
        "//:resolved_address",
        "//:resource_quota",
        "//:slice",
        "//:slice_refcount",
        "//:time",
        "//:useful",
    ],
)

grpc_cc_library(
    name = "grpc_test_util",
    srcs = TEST_UTILS_THAT_USE_GRPC_H_SOURCES + ["tls_utils.cc"],
    hdrs = TEST_UTILS_THAT_USE_GRPC_H_HEADERS + ["tls_utils.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/debugging:failure_signal_handler",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "C++",
    deps = [
        "build",
        "stack_tracer",
        "//:channel_args_preconditioning",
        "//:config",
        "//:gpr",
        "//:gpr_atm",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:grpc_sockaddr",
        "//:grpc_trace",
        "//:httpcli",
        "//:iomgr_fwd",
        "//:iomgr_port",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:resolved_address",
        "//:slice",
        "//:slice_refcount",
        "//:time",
        "//:tsi_ssl_credentials",
        "//:uri_parser",
        "//test/core/event_engine:test_init",
    ],
)

grpc_cc_library(
    name = "grpc_test_util_unsecure",
    srcs = TEST_UTILS_THAT_USE_GRPC_H_SOURCES,
    hdrs = TEST_UTILS_THAT_USE_GRPC_H_HEADERS,
    external_deps = [
        "absl/debugging:failure_signal_handler",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "C++",
    deps = [
        "build",
        "stack_tracer",
        "//:channel_args_preconditioning",
        "//:config",
        "//:gpr",
        "//:gpr_atm",
        "//:grpc_security_base",
        "//:grpc_sockaddr",
        "//:grpc_unsecure",
        "//:httpcli",
        "//:iomgr_fwd",
        "//:iomgr_port",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:resolved_address",
        "//:time",
        "//:uri_parser",
        "//test/core/event_engine:test_init",
    ],
)

grpc_cc_test(
    name = "cmdline_test",
    srcs = ["cmdline_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "grpc_test_util",
        "grpc_test_util_base",
        "//:gpr",
        "//:useful",
    ],
)

grpc_cc_library(
    name = "fuzzer_corpus_test",
    testonly = 1,
    srcs = ["fuzzer_corpus_test.cc"],
    external_deps = [
        "absl/flags:flag",
        "gtest",
    ],
    linkstatic = True,
    tags = [
        "no_windows",
        "nofixdeps",
    ],
    deps = [
        ":grpc_test_util",
        "//:grpc",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_test(
    name = "histogram_test",
    srcs = ["histogram_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = ["nofixdeps"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "grpc_test_util",
        "grpc_test_util_base",
        "//:gpr",
    ],
)

sh_library(
    name = "fuzzer_one_entry_runner",
    srcs = ["fuzzer_one_entry_runner.sh"],
)

grpc_cc_library(
    name = "stack_tracer",
    srcs = [
        "stack_tracer.cc",
    ],
    hdrs = [
        "stack_tracer.h",
    ],
    external_deps = [
        "absl/debugging:stacktrace",
        "absl/debugging:symbolize",
    ],
    language = "C++",
    deps = [
        "//:examine_stack",
        "//:gpr_platform",
    ],
)

grpc_cc_test(
    name = "stack_tracer_test",
    srcs = [
        "stack_tracer_test.cc",
    ],
    external_deps = [
        "absl/strings",
        "gtest",
    ],
    language = "C++",
    # TODO(https://github.com/grpc/grpc/issues/24627): Disable this on Windows
    tags = ["no_windows"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "grpc_test_util",
        "stack_tracer",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "test_lb_policies",
    testonly = 1,
    srcs = ["test_lb_policies.cc"],
    hdrs = ["test_lb_policies.h"],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:variant",
    ],
    deps = [
        "//:config",
        "//:gpr",
        "//:grpc",
        "//:grpc_backend_metric_data",
        "//:grpc_client_channel",
        "//:json",
        "//:json_util",
        "//:lb_policy",
        "//:lb_policy_factory",
        "//:lb_policy_registry",
        "//:orphanable",
        "//:pollset_set",
        "//:ref_counted_ptr",
        "//:resolved_address",
        "//:server_address",
        "//:subchannel_interface",
        "//:time",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "fake_udp_and_tcp_server",
    srcs = ["fake_udp_and_tcp_server.cc"],
    hdrs = ["fake_udp_and_tcp_server.h"],
    external_deps = [
        "absl/memory",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "C++",
    deps = [
        "grpc_test_util",
        "//:gpr",
        "//:grpc",
        "//:resolved_address",
        "//:sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "build",
    srcs = ["build.cc"],
    hdrs = ["build.h"],
)
