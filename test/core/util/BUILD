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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package", "grpc_proto_library")

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
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:iomgr_timer",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:closure",
        "//src/core:grpc_authorization_base",
        "//src/core:grpc_sockaddr",
        "//src/core:iomgr_fwd",
        "//src/core:memory_quota",
        "//src/core:resolved_address",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:time",
        "//src/core:useful",
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
        "//:config",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:httpcli",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:tsi_ssl_credentials",
        "//:uri_parser",
        "//src/core:channel_args_endpoint_config",
        "//src/core:channel_args_preconditioning",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:gpr_atm",
        "//src/core:grpc_sockaddr",
        "//src/core:iomgr_fwd",
        "//src/core:iomgr_port",
        "//src/core:resolved_address",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:status_helper",
        "//src/core:time",
        "//test/core/event_engine:test_init",
    ],
    alwayslink = 1,
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
        "//:config",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_security_base",
        "//:grpc_unsecure",
        "//:httpcli",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:uri_parser",
        "//src/core:channel_args_endpoint_config",
        "//src/core:channel_args_preconditioning",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:gpr_atm",
        "//src/core:grpc_sockaddr",
        "//src/core:iomgr_fwd",
        "//src/core:iomgr_port",
        "//src/core:resolved_address",
        "//src/core:status_helper",
        "//src/core:time",
        "//test/core/event_engine:test_init",
    ],
    alwayslink = 1,
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
        "//src/core:useful",
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
        "//:gpr_platform",
        "//src/core:examine_stack",
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
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:variant",
    ],
    deps = [
        "//:config",
        "//:gpr",
        "//:grpc",
        "//:grpc_client_channel",
        "//:orphanable",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:uri_parser",
        "//src/core:channel_args",
        "//src/core:delegating_helper",
        "//src/core:error",
        "//src/core:grpc_backend_metric_data",
        "//src/core:json",
        "//src/core:json_util",
        "//src/core:lb_policy",
        "//src/core:lb_policy_factory",
        "//src/core:lb_policy_registry",
        "//src/core:pollset_set",
        "//src/core:resolved_address",
        "//src/core:status_helper",
        "//src/core:subchannel_interface",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "fake_udp_and_tcp_server",
    srcs = ["fake_udp_and_tcp_server.cc"],
    hdrs = ["fake_udp_and_tcp_server.h"],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "C++",
    deps = [
        "grpc_test_util",
        "//:gpr",
        "//:grpc",
        "//:sockaddr_utils",
        "//src/core:resolved_address",
    ],
)

grpc_cc_library(
    name = "fake_udp_and_tcp_server_unsecure",
    srcs = ["fake_udp_and_tcp_server.cc"],
    hdrs = ["fake_udp_and_tcp_server.h"],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "C++",
    deps = [
        "grpc_test_util_unsecure",
        "//:gpr",
        "//:grpc",
        "//:sockaddr_utils",
        "//src/core:resolved_address",
    ],
)

grpc_cc_library(
    name = "socket_use_after_close_detector",
    srcs = ["socket_use_after_close_detector.cc"],
    hdrs = ["socket_use_after_close_detector.h"],
    external_deps = [
        "absl/flags:flag",
        "absl/memory",
        "absl/strings",
        "absl/strings:str_format",
        "gtest",
    ],
    language = "C++",
    deps = [
        "grpc_test_util",
        "//:gpr",
        "//:grpc",
        "//:parse_address",
        "//:server_address",
        "//:sockaddr_utils",
        "//src/core:default_event_engine",
        "//src/core:grpc_sockaddr",
    ],
)

grpc_cc_library(
    name = "socket_use_after_close_detector_unsecure",
    srcs = ["socket_use_after_close_detector.cc"],
    hdrs = ["socket_use_after_close_detector.h"],
    external_deps = [
        "absl/flags:flag",
        "absl/memory",
        "absl/strings",
        "absl/strings:str_format",
        "gtest",
    ],
    language = "C++",
    deps = [
        "grpc_test_util_unsecure",
        "//:gpr",
        "//:grpc",
        "//:parse_address",
        "//:server_address",
        "//:sockaddr_utils",
        "//src/core:default_event_engine",
        "//src/core:grpc_sockaddr",
    ],
)

grpc_cc_library(
    name = "build",
    srcs = ["build.cc"],
    hdrs = ["build.h"],
)

grpc_cc_library(
    name = "osa_distance",
    srcs = ["osa_distance.cc"],
    hdrs = ["osa_distance.h"],
    external_deps = ["absl/strings"],
)

grpc_cc_test(
    name = "osa_distance_test",
    srcs = ["osa_distance_test.cc"],
    external_deps = ["gtest"],
    deps = ["osa_distance"],
)

grpc_cc_library(
    name = "scoped_env_var",
    testonly = True,
    hdrs = ["scoped_env_var.h"],
    deps = [
        "//:gpr_platform",
        "//src/core:env",
    ],
)

grpc_proto_library(
    name = "fuzz_config_vars_proto",
    srcs = ["fuzz_config_vars.proto"],
    has_services = False,
)

grpc_cc_library(
    name = "fuzz_config_vars",
    srcs = ["fuzz_config_vars.cc"],
    hdrs = ["fuzz_config_vars.h"],
    external_deps = ["absl/types:optional"],
    deps = [
        "fuzz_config_vars_helpers",
        "fuzz_config_vars_proto",
        "//:config_vars",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "fuzz_config_vars_helpers",
    srcs = ["fuzz_config_vars_helpers.cc"],
    hdrs = ["fuzz_config_vars_helpers.h"],
    external_deps = ["absl/strings"],
    deps = [
        "//:gpr_platform",
        "//src/core:experiments",
    ],
)

grpc_cc_library(
    name = "audit_logging_utils",
    srcs = ["audit_logging_utils.cc"],
    hdrs = ["audit_logging_utils.h"],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "//:gpr",
        "//:gpr_platform",
        "//:grpc",
        "//src/core:json_writer",
    ],
)

grpc_proto_library(
    name = "fuzzing_channel_args_proto",
    srcs = ["fuzzing_channel_args.proto"],
    has_services = False,
)

grpc_cc_library(
    name = "fuzzing_channel_args",
    srcs = ["fuzzing_channel_args.cc"],
    hdrs = ["fuzzing_channel_args.h"],
    deps = [
        "fuzzing_channel_args_proto",
        "//:gpr_platform",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:resource_quota",
    ],
)
