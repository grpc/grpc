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

COMMON_HEADERS = [
    "event_engine_test.h",
    "event_engine_test_utils.h",
]

grpc_cc_library(
    name = "timer",
    testonly = True,
    srcs = ["timer_test.cc"],
    hdrs = COMMON_HEADERS,
    deps = [":conformance_test_base_lib"],
    alwayslink = 1,
)

grpc_cc_library(
    name = "dns",
    testonly = True,
    srcs = ["dns_test.cc"],
    hdrs = COMMON_HEADERS,
    deps = [":conformance_test_base_lib"],
    alwayslink = 1,
)

grpc_cc_library(
    name = "client",
    testonly = True,
    srcs = ["client_test.cc"],
    hdrs = COMMON_HEADERS,
    deps = [":conformance_test_base_lib"],
    alwayslink = 1,
)

grpc_cc_library(
    name = "server",
    testonly = True,
    srcs = ["server_test.cc"],
    hdrs = COMMON_HEADERS,
    deps = [":conformance_test_base_lib"],
    alwayslink = 1,
)

grpc_cc_library(
    name = "complete",
    testonly = 1,
    hdrs = COMMON_HEADERS,
    deps = [
        ":client",
        ":dns",
        ":server",
        ":timer",
    ],
    alwayslink = 1,
)

# -- EventEngine implementations --

grpc_cc_test(
    name = "posix_event_engine_test",
    srcs = ["posix_event_engine_test.cc"],
    uses_polling = False,
    deps = [
        "//:posix_event_engine",
        "//test/core/event_engine/test_suite:timer",
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
        "//:windows_event_engine",
        "//test/core/event_engine/test_suite:timer",
    ],
)

grpc_cc_test(
    name = "fuzzing_event_engine_test",
    srcs = ["fuzzing_event_engine_test.cc"],
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_polling = False,
    deps = [
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/test_suite:timer",
    ],
)

# -- Internal targets --

grpc_cc_library(
    name = "oracle_event_engine_posix",
    testonly = True,
    srcs = ["oracle_event_engine_posix.cc"],
    hdrs = ["oracle_event_engine_posix.h"],
    tags = [
        "cpu:10",
        "no_windows",
    ],
    deps = [
        ":conformance_test_base_lib",
        "//:grpc",
        "//test/core/util:grpc_test_util",
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
        ":client",
        ":oracle_event_engine_posix",
        "//:grpc",
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
    hdrs = COMMON_HEADERS,
    external_deps = ["gtest"],
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)
