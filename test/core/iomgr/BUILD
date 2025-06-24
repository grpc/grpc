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

load("//bazel:custom_exec_properties.bzl", "LARGE_MACHINE")
load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/core/iomgr",
    visibility = "public",
)  # Useful for third party devs to test their io manager implementation.

grpc_cc_library(
    name = "endpoint_tests",
    srcs = ["endpoint_tests.cc"],
    hdrs = ["endpoint_tests.h"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
    ],
    visibility = [
        "//bazel:endpoint_tests",
        "//test:__subpackages__",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "combiner_test",
    srcs = ["combiner_test.cc"],
    exec_properties = LARGE_MACHINE,
    external_deps = ["gtest"],
    flaky = True,
    tags = ["no_windows"],  # LARGE_MACHINE is not configured for windows RBE
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "endpoint_pair_test",
    srcs = ["endpoint_pair_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
    ],
    tags = [
        "endpoint_test",
        "event_engine_client_test",
    ],
    deps = [
        ":endpoint_tests",
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "error_test",
    srcs = ["error_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    tags = ["error_tests"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":endpoint_tests",
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "fd_conservation_posix_test",
    srcs = ["fd_conservation_posix_test.cc"],
    external_deps = ["absl/log:check"],
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "fd_posix_test",
    srcs = ["fd_posix_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    tags = [
        "no_windows",
        "requires-net:ipv4",
        "requires-net:loopback",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "grpc_ipv6_loopback_available_test",
    srcs = ["grpc_ipv6_loopback_available_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "resolve_address_using_ares_resolver_posix_test",
    srcs = ["resolve_address_posix_test.cc"],
    args = [
        "--resolver=ares",
    ],
    external_deps = [
        "absl/flags:flag",
        "absl/flags:parse",
        "absl/log:log",
        "absl/strings",
        "gtest",
    ],
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "resolve_address_using_native_resolver_posix_test",
    srcs = ["resolve_address_posix_test.cc"],
    args = [
        "--resolver=native",
    ],
    external_deps = [
        "absl/flags:flag",
        "absl/flags:parse",
        "absl/log:log",
        "absl/strings",
        "gtest",
    ],
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "resolve_address_using_ares_resolver_test",
    srcs = ["resolve_address_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
        "gtest",
    ],
    tags = [
        # Requires ares
        "grpc:broken-internally",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:fake_udp_and_tcp_server",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_test(
    name = "resolve_address_using_native_resolver_test",
    srcs = ["resolve_address_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
        "gtest",
    ],
    tags = [
        # Requires ares
        "grpc:broken-internally",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:fake_udp_and_tcp_server",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_test(
    name = "socket_utils_test",
    srcs = ["socket_utils_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    tags = [
        "no_windows",
        "requires-net:ipv4",
        "requires-net:loopback",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "tcp_client_posix_test",
    srcs = ["tcp_client_posix_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    tags = [
        "endpoint_test",
        "event_engine_client_test",
        "no_windows",
        "requires-net:ipv4",
        "requires-net:loopback",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "tcp_posix_test",
    srcs = ["tcp_posix_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
    ],
    tags = [
        "endpoint_test",
        "event_engine_listener_test",
        "no_mac",  # TODO(jtattermusch): Reenable once https://github.com/grpc/grpc/issues/21282 is fixed.
        "no_windows",
        "requires-net:ipv4",
        "requires-net:loopback",
    ],
    deps = [
        ":endpoint_tests",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "buffer_list_test",
    srcs = ["buffer_list_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "pollset_windows_starvation_test",
    srcs = ["pollset_windows_starvation_test.cc"],
    tags = [
        "no_linux",
        "no_mac",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "tcp_server_posix_test",
    srcs = ["tcp_server_posix_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    tags = [
        "event_engine_listener_test",
        "no_windows",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "timer_heap_test",
    srcs = ["timer_heap_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)
