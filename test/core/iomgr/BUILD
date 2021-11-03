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
load("//bazel:custom_exec_properties.bzl", "LARGE_MACHINE")

licenses(["notice"])

load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer")

grpc_package(
    name = "test/core/iomgr",
    visibility = "public",
)  # Useful for third party devs to test their io manager implementation.

grpc_cc_library(
    name = "endpoint_tests",
    srcs = ["endpoint_tests.cc"],
    hdrs = ["endpoint_tests.h"],
    language = "C++",
    visibility = [
        "//test:__subpackages__",
        "@grpc:endpoint_tests",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "combiner_test",
    srcs = ["combiner_test.cc"],
    exec_properties = LARGE_MACHINE,
    language = "C++",
    tags = ["no_windows"],  # LARGE_MACHINE is not configured for windows RBE
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "endpoint_pair_test",
    srcs = ["endpoint_pair_test.cc"],
    language = "C++",
    deps = [
        ":endpoint_tests",
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "error_test",
    srcs = ["error_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        ":endpoint_tests",
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "ev_epollex_linux_test",
    srcs = ["ev_epollex_linux_test.cc"],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "fd_conservation_posix_test",
    srcs = ["fd_conservation_posix_test.cc"],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "fd_posix_test",
    srcs = ["fd_posix_test.cc"],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "grpc_ipv6_loopback_available_test",
    srcs = ["grpc_ipv6_loopback_available_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "load_file_test",
    srcs = ["load_file_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "work_serializer_test",
    srcs = ["work_serializer_test.cc"],
    exec_properties = LARGE_MACHINE,
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = [
        "no_windows",  # LARGE_MACHINE is not configured for windows RBE
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "mpmcqueue_test",
    srcs = ["mpmcqueue_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "resolve_address_using_ares_resolver_posix_test",
    srcs = ["resolve_address_posix_test.cc"],
    args = [
        "--resolver=ares",
    ],
    external_deps = [
        "absl/strings",
    ],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "resolve_address_using_native_resolver_posix_test",
    srcs = ["resolve_address_posix_test.cc"],
    args = [
        "--resolver=native",
    ],
    external_deps = [
        "absl/strings",
    ],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "resolve_address_using_ares_resolver_test",
    srcs = ["resolve_address_test.cc"],
    args = [
        "--resolver=ares",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:fake_udp_and_tcp_server",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "resolve_address_using_native_resolver_test",
    srcs = ["resolve_address_test.cc"],
    args = [
        "--resolver=native",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:fake_udp_and_tcp_server",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "resource_quota_test",
    srcs = ["resource_quota_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "socket_utils_test",
    srcs = ["socket_utils_test.cc"],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "tcp_client_posix_test",
    srcs = ["tcp_client_posix_test.cc"],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "tcp_posix_test",
    srcs = ["tcp_posix_test.cc"],
    language = "C++",
    tags = [
        "no_mac",  # TODO(jtattermusch): Reenable once https://github.com/grpc/grpc/issues/21282 is fixed.
        "no_windows",
    ],
    deps = [
        ":endpoint_tests",
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "buffer_list_test",
    srcs = ["buffer_list_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "tcp_server_posix_test",
    srcs = ["tcp_server_posix_test.cc"],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "threadpool_test",
    srcs = ["threadpool_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "time_averaged_stats_test",
    srcs = ["time_averaged_stats_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "timer_heap_test",
    srcs = ["timer_heap_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "timer_list_test",
    srcs = ["timer_list_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "stranded_event_test",
    srcs = ["stranded_event_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = [
        # TODO(apolcyn): This test is failing on Windows at entry, enable once passing.
        # See e.g. https://source.cloud.google.com/results/invocations/6716596a-c9e1-4780-85ed-890d8758d582/targets
        "no_windows",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/end2end:cq_verifier",
        "//test/core/util:grpc_test_util",
    ],
)
