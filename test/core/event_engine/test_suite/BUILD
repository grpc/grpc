# Copyright 2022 gRPC authors.
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

grpc_package(name = "test/core/event_engine/test_suite")

grpc_cc_library(
    name = "timer",
    testonly = True,
    srcs = ["timer_test.cc"],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:bind_front",
        "absl/time",
        "gtest",
    ],
    deps = [
        "conformance_test_base_lib",
        "//:gpr",
        "//src/core:event_engine_interface",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "dns",
    testonly = True,
    srcs = ["dns_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "conformance_test_base_lib",
        "//:exec_ctx",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "client",
    testonly = True,
    srcs = ["client_test.cc"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/time",
        "gtest",
    ],
    deps = [
        "conformance_test_base_lib",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_unsecure",
        "//src/core:channel_args",
        "//src/core:memory_quota",
        "//src/core:notification",
        "//src/core:resource_quota",
        "//test/core/util:grpc_test_util_unsecure",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "server",
    testonly = True,
    srcs = ["server_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "conformance_test_base_lib",
        "//:exec_ctx",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "complete",
    testonly = 1,
    alwayslink = 1,
)

# -- EventEngine implementations --

grpc_cc_test(
    name = "posix_event_engine_test",
    srcs = ["posix_event_engine_test.cc"],
    external_deps = ["gtest"],
    tags = [
        "no_windows",
    ],
    uses_polling = False,
    deps = [
        "conformance_test_base_lib",
        "//:grpc_unsecure",
        "//src/core:event_engine_interface",
        "//src/core:posix_event_engine",
        "//test/core/util:grpc_test_util_unsecure",
    ],
)

grpc_cc_test(
    name = "windows_event_engine_test",
    srcs = ["windows_event_engine_test.cc"],
    tags = [
        "bazel_only",
        "no_linux",
        "no_mac",
    ],
    uses_polling = False,
    deps = [
        "conformance_test_base_lib",
        "//:gpr_platform",
        "//:grpc",
        "//src/core:windows_event_engine",
        "//test/core/util:grpc_test_util_unsecure",
    ],
)

grpc_cc_test(
    name = "fuzzing_event_engine_test",
    srcs = ["fuzzing_event_engine_test.cc"],
    external_deps = [
        "absl/time",
        "gtest",
    ],
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_polling = False,
    deps = [
        "conformance_test_base_lib",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
    ],
)

# -- Internal targets --

grpc_cc_library(
    name = "oracle_event_engine_posix",
    testonly = True,
    srcs = ["oracle_event_engine_posix.cc"],
    hdrs = ["oracle_event_engine_posix.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/time",
    ],
    tags = [
        "cpu:10",
        "no_windows",
    ],
    visibility = ["//test:__subpackages__"],
    deps = [
        "conformance_test_base_lib",
        "//:gpr",
        "//:sockaddr_utils",
        "//src/core:event_engine_interface",
        "//src/core:event_engine_memory_allocator",
        "//src/core:event_engine_slice_buffer",
        "//src/core:notification",
        "//src/core:resolved_address",
        "//src/core:strerror",
    ],
)

grpc_cc_test(
    name = "oracle_event_engine_posix_test",
    srcs = ["oracle_event_engine_posix_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "no_test_ios",
        "no_windows",
    ],
    # TODO(vignesbabu): This is required because the oracle event engine uses
    # poll syscall. If uses_polling is set to False, there will be an attempt
    # to run this test with GRPC_POLL_STRATEGY=none which will hijack the poll
    # c-wrapper causing the test to fail. A more generic posix oracle event
    # engine design which doesn't rely on poll is required.
    uses_polling = True,
    deps = [
        "conformance_test_base_lib",
        "oracle_event_engine_posix",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "conformance_test_base_lib",
    testonly = True,
    srcs = [
        "event_engine_test.cc",
        "event_engine_test_utils.cc",
    ],
    hdrs = [
        "event_engine_test.h",
        "event_engine_test_utils.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/time",
        "gtest",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:uri_parser",
        "//src/core:memory_quota",
        "//src/core:notification",
        "//src/core:resolved_address",
    ],
)
